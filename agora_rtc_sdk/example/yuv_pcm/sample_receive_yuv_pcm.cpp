//  Agora RTC/MEDIA SDK
//
//  Created by Jay Zhang in 2020-04.
//  Copyright (c) 2020 Agora.io. All rights reserved.
//

#include <csignal>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <ctime>

#include "AgoraRefCountedObject.h"
#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "common/log.h"
#include "common/opt_parser.h"
#include "common/sample_common.h"
#include "common/sample_connection_observer.h"
#include "common/sample_local_user_observer.h"

#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"
#include "NGIAgoraVideoTrack.h"

#define DEFAULT_SAMPLE_RATE (16000)
#define DEFAULT_NUM_OF_CHANNELS (1)
#define DEFAULT_AUDIO_FILE "received_audio.pcm"
#define DEFAULT_VIDEO_FILE "video/received_video"
#define DEFAULT_FILE_LIMIT (100 * 1024 * 1024)
#define STREAM_TYPE_HIGH "high"
#define STREAM_TYPE_LOW "low"

int time_20_s = 20;
int time_2_s = 2;
static bool video_frame_saved_flag = 0;

struct SampleOptions
{
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string remoteUserId;
  std::string streamType = STREAM_TYPE_HIGH;
  std::string audioFile = DEFAULT_AUDIO_FILE;
  std::string videoFile = DEFAULT_VIDEO_FILE;

  struct
  {
    int sampleRate = DEFAULT_SAMPLE_RATE;
    int numOfChannels = DEFAULT_NUM_OF_CHANNELS;
  } audio;
};

class PcmFrameObserver : public agora::media::IAudioFrameObserverBase
{
public:
  PcmFrameObserver(const std::string &outputFilePath)
      : outputFilePath_(outputFilePath),
        pcmFile_(nullptr),
        fileCount(0),
        fileSize_(0) {}

  bool onPlaybackAudioFrame(const char *channelId, AudioFrame &audioFrame) override { return true; };

  bool onRecordAudioFrame(const char *channelId, AudioFrame &audioFrame) override { return true; };

  bool onMixedAudioFrame(const char *channelId, AudioFrame &audioFrame) override { return true; };

  bool onPlaybackAudioFrameBeforeMixing(const char *channelId, agora::media::base::user_id_t userId, AudioFrame &audioFrame) override;

  bool onEarMonitoringAudioFrame(AudioFrame &audioFrame) { return true; };

private:
  std::string outputFilePath_;
  FILE *pcmFile_;
  int fileCount;
  int fileSize_;
};

class YuvFrameObserver : public agora::rtc::IVideoFrameObserver2
{
public:
  YuvFrameObserver(const std::string &outputFilePath)
      : outputFilePath_(outputFilePath),
        yuvFile_(nullptr),
        fileCount(0),
        fileSize_(0) {}

  void onFrame(const char *channelId, agora::user_id_t remoteUid, const agora::media::base::VideoFrame *frame) override;

  virtual ~YuvFrameObserver() = default;

private:
  std::string outputFilePath_;
  FILE *yuvFile_;
  int fileCount;
  int fileSize_;
};

bool PcmFrameObserver::onPlaybackAudioFrameBeforeMixing(const char *channelId, agora::media::base::user_id_t userId, AudioFrame &audioFrame)
{
  // Create new file to save received PCM samples
  if (!pcmFile_)
  {
    std::string fileName = (++fileCount > 1)
                               ? (outputFilePath_ + to_string(fileCount))
                               : outputFilePath_;
    if (!(pcmFile_ = fopen(fileName.c_str(), "w")))
    {
      AG_LOG(ERROR, "Failed to create received audio file %s",
             fileName.c_str());
      return false;
    }
    AG_LOG(INFO, "Created file %s to save received PCM samples",
           fileName.c_str());
  }

  // Write PCM samples
  size_t writeBytes =
      audioFrame.samplesPerChannel * audioFrame.channels * sizeof(int16_t);
  if (fwrite(audioFrame.buffer, 1, writeBytes, pcmFile_) != writeBytes)
  {
    AG_LOG(ERROR, "Error writing decoded audio data: %s", std::strerror(errno));
    return false;
  }
  fileSize_ += writeBytes;

  // Close the file if size limit is reached
  if (fileSize_ >= DEFAULT_FILE_LIMIT)
  {
    fclose(pcmFile_);
    pcmFile_ = nullptr;
    fileSize_ = 0;
  }
  return true;
}

void YuvFrameObserver::onFrame(const char *channelId, agora::user_id_t remoteUid, const agora::media::base::VideoFrame *videoFrame)
{
  // check to see if frame is already saved
  if (video_frame_saved_flag)
    return;

  // Create new file to save received YUV frames
  if (!yuvFile_)
  {
    std::string fileName = (++fileCount > 1)
                               ? (outputFilePath_ + "_" + channelId + "_" + to_string(fileCount))
                               : outputFilePath_ + "_" + channelId + "_" + to_string(time(0)) + ".yuv";
    if (!(yuvFile_ = fopen(fileName.c_str(), "w+")))
    {
      AG_LOG(ERROR, "Failed to create received video file %s",
             fileName.c_str());
      return;
    }
    AG_LOG(INFO, "Created file %s to save received YUV frames",
           fileName.c_str());
  }

  // Write Y planar
  size_t writeBytes = videoFrame->yStride * videoFrame->height;
  if (fwrite(videoFrame->yBuffer, 1, writeBytes, yuvFile_) != writeBytes)
  {
    AG_LOG(ERROR, "Error writing decoded video data: %s", std::strerror(errno));
    return;
  }
  fileSize_ += writeBytes;

  // Write U planar
  writeBytes = videoFrame->uStride * videoFrame->height / 2;
  if (fwrite(videoFrame->uBuffer, 1, writeBytes, yuvFile_) != writeBytes)
  {
    AG_LOG(ERROR, "Error writing decoded video data: %s", std::strerror(errno));
    return;
  }
  fileSize_ += writeBytes;

  // Write V planar
  writeBytes = videoFrame->vStride * videoFrame->height / 2;
  if (fwrite(videoFrame->vBuffer, 1, writeBytes, yuvFile_) != writeBytes)
  {
    AG_LOG(ERROR, "Error writing decoded video data: %s", std::strerror(errno));
    return;
  }
  fileSize_ += writeBytes;
  video_frame_saved_flag = 1;
  // Close the file if size limit is reached
  if (fileSize_ >= DEFAULT_FILE_LIMIT)
  {
    fclose(yuvFile_);
    yuvFile_ = nullptr;
    fileSize_ = 0;
  }
  return;
};

static bool exitFlag = false;
static void SignalHandler(int sigNo) { exitFlag = true; }

int main(int argc, char *argv[])
{
  SampleOptions options;
  opt_parser optParser;
  time_t current_conn_time;

  optParser.add_long_opt("token", &options.appId,
                         "The token for authentication");
  optParser.add_long_opt("channelId", &options.channelId, "Channel Id");
  optParser.add_long_opt("userId", &options.userId, "User Id / default is 0");
  optParser.add_long_opt("remoteUserId", &options.remoteUserId,
                         "The remote user to receive stream from");
  optParser.add_long_opt("audioFile", &options.audioFile, "Output audio file");
  optParser.add_long_opt("videoFile", &options.videoFile, "Output video file");
  optParser.add_long_opt("sampleRate", &options.audio.sampleRate,
                         "Sample rate for received audio");
  optParser.add_long_opt("numOfChannels", &options.audio.numOfChannels,
                         "Number of channels for received audio");
  optParser.add_long_opt("streamtype", &options.streamType, "the stream type");

  if ((argc <= 1) || !optParser.parse_opts(argc, argv))
  {
    std::ostringstream strStream;
    optParser.print_usage(argv[0], strStream);
    std::cout << strStream.str() << std::endl;
    return -1;
  }

  if (options.appId.empty())
  {
    AG_LOG(ERROR, "Must provide appId!");
    return -1;
  }

  if (options.channelId.empty())
  {
    AG_LOG(ERROR, "Must provide channelId!");
    return -1;
  }

  std::signal(SIGQUIT, SignalHandler);
  std::signal(SIGABRT, SignalHandler);
  std::signal(SIGINT, SignalHandler);

  // Create Agora service
  auto service = createAndInitAgoraService(false, true, true);
  if (!service)
  {
    AG_LOG(ERROR, "Failed to creating Agora service!");
  }

  // Create Agora connection
  agora::rtc::AudioSubscriptionOptions audioSubOpt;
  audioSubOpt.bytesPerSample = sizeof(int16_t) * options.audio.numOfChannels;
  audioSubOpt.numberOfChannels = options.audio.numOfChannels;
  audioSubOpt.sampleRateHz = options.audio.sampleRate;

  agora::rtc::RtcConnectionConfiguration ccfg;
  ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_AUDIENCE;
  ccfg.audioSubscriptionOptions = audioSubOpt;
  ccfg.autoSubscribeAudio = false;
  ccfg.autoSubscribeVideo = false;
  ccfg.enableAudioRecordingOrPlayout =
      false; // Subscribe audio but without playback

  //start the connect -> save frame -> disconnect loop
  do
  {
    agora::agora_refptr<agora::rtc::IRtcConnection> connection =
        service->createRtcConnection(ccfg);
    if (!connection)
    {
      AG_LOG(ERROR, "Failed to creating Agora connection!");
      return -1;
    }

    // Subcribe streams from all remote users or specific remote user
    agora::rtc::VideoSubscriptionOptions subscriptionOptions;
    if (options.streamType == STREAM_TYPE_HIGH)
    {
      subscriptionOptions.type = agora::rtc::VIDEO_STREAM_HIGH;
    }
    else if (options.streamType == STREAM_TYPE_LOW)
    {
      subscriptionOptions.type = agora::rtc::VIDEO_STREAM_LOW;
    }
    else
    {
      AG_LOG(ERROR, "It is a error stream type");
      return -1;
    }
    if (options.remoteUserId.empty())
    {
      AG_LOG(INFO, "Subscribe streams from all remote users");
      connection->getLocalUser()->subscribeAllAudio();
      connection->getLocalUser()->subscribeAllVideo(subscriptionOptions);
    }
    else
    {
      connection->getLocalUser()->subscribeAudio(options.remoteUserId.c_str());
      connection->getLocalUser()->subscribeVideo(options.remoteUserId.c_str(),
                                                 subscriptionOptions);
    }
    // Register connection observer to monitor connection event
    auto connObserver = std::make_shared<SampleConnectionObserver>();
    connection->registerObserver(connObserver.get());

    // Create local user observer
    auto localUserObserver =
        std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());

    // Register audio frame observer to receive audio stream
    auto pcmFrameObserver = std::make_shared<PcmFrameObserver>(options.audioFile);

    if (connection->getLocalUser()->setPlaybackAudioFrameBeforeMixingParameters(
            options.audio.numOfChannels, options.audio.sampleRate))
    {
      AG_LOG(ERROR, "Failed to set audio frame parameters!");
      return -1;
    }
    localUserObserver->setAudioFrameObserver(pcmFrameObserver.get());

    // Register video frame observer to receive video stream
    std::shared_ptr<YuvFrameObserver> yuvFrameObserver =
        std::make_shared<YuvFrameObserver>(options.videoFile);
    localUserObserver->setVideoFrameObserver(yuvFrameObserver.get());

    // Connect to Agora channel
    if (connection->connect(options.appId.c_str(), options.channelId.c_str(),
                            options.userId.c_str()))
    {
      AG_LOG(ERROR, "Failed to connect to Agora channel!");
      return -1;
    }

    // reset timer and flag
    current_conn_time = time(0);
    video_frame_saved_flag = 0;

    // Start receiving incoming media data
    AG_LOG(INFO, "Start receiving audio & video data ...");

    // Periodically check if in the channel for 2s
    while (time(0) - current_conn_time <=0 /*time_2_s*/)
    {
      usleep(100000);
    }
    // Unregister audio & video frame observers
    localUserObserver->unsetAudioFrameObserver();
    localUserObserver->unsetVideoFrameObserver();

    // Unregister connection observer
    connection->unregisterObserver(connObserver.get());

    // Disconnect from Agora channel
    if (connection->disconnect())
    {
      AG_LOG(ERROR, "Failed to disconnect from Agora channel!");
      return -1;
    }
    AG_LOG(INFO, "Disconnected from Agora channel successfully");

    // Destroy Agora connection and related resources
    localUserObserver.reset();
    pcmFrameObserver.reset();
    yuvFrameObserver.reset();
    connection = nullptr;

    // Periodically check if it has been 20s
    while ((time(0) - current_conn_time) < time_20_s)
    {
      usleep(500000);
    }
  } while (!exitFlag); //check exit flag

  // while ((time(0) - current_conn_time) > time_20_s);
  // Destroy Agora Service
  service->release();
  service = nullptr;

  return 0;
}
