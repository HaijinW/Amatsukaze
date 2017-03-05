#pragma once

#include <string>
#include <sstream>
#include <memory>

#include "StreamUtils.hpp"
#include "TsSplitter.hpp"

// �J���[�X�y�[�X��`���g������
#include "libavutil/pixfmt.h"

namespace av {

// �J���[�X�y�[�X3�Z�b�g
// x265�͐��l���̂܂܂ł�OK�����Ax264��help���������string�łȂ����
// �Ȃ�Ȃ��悤�Ȃ̂ŕϊ����`
// �Ƃ肠����ARIB STD-B32 v3.7�ɏ����Ă���̂���

// 3���F
static const char* getColorPrimStr(int color_prim) {
  switch (color_prim) {
  case AVCOL_PRI_BT709: return "bt709";
  case AVCOL_PRI_BT2020: return "bt2020";
  default:
    THROWF(FormatException,
      "Unsupported color primaries (%d)", color_prim);
  }
  return NULL;
}

// �K���}
static const char* getTransferCharacteristicsStr(int transfer_characteritics) {
  switch (transfer_characteritics) {
  case AVCOL_TRC_BT709: return "bt709";
  case AVCOL_TRC_IEC61966_2_4: return "iec61966-2-4";
  case AVCOL_TRC_BT2020_10: return "bt2020-10";
  case AVCOL_TRC_SMPTEST2084: return "smpte-st-2084";
  case AVCOL_TRC_ARIB_STD_B67: return "arib-std-b67";
  default:
    THROWF(FormatException,
      "Unsupported color transfer characteritics (%d)", transfer_characteritics);
  }
  return NULL;
}

// �ϊ��W��
static const char* getColorSpaceStr(int color_space) {
  switch (color_space) {
  case AVCOL_SPC_BT709: return "bt709";
  case AVCOL_SPC_BT2020_NCL: return "bt2020nc";
  default:
    THROWF(FormatException,
      "Unsupported color color space (%d)", color_space);
  }
  return NULL;
}

} // namespace av {

static std::string makeArgs(
  const std::string& binpath,
  const std::string& options,
  const VideoFormat& fmt,
  const std::string& outpath)
{
  std::ostringstream ss;

  ss << "\"" << binpath << "\"";
  ss << " --demuxer y4m";

  // y4m�w�b�_�ɂ���̂ŕK�v�Ȃ�
  //ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
  //ss << " --input-res " << fmt.width << "x" << fmt.height;
  //ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

  ss << " --colorprim " << av::getColorPrimStr(fmt.colorPrimaries);
  ss << " --transfer " << av::getTransferCharacteristicsStr(fmt.transferCharacteristics);
  ss << " --colormatrix " << av::getColorSpaceStr(fmt.colorSpace);

  if (fmt.progressive == false) {
    ss << " --tff";
  }

  ss << " " << options << " -o \"" << outpath << "\" -";
  
  return ss.str();
}

static std::string makeIntVideoFilePath(const std::string& basepath, int index)
{
  std::ostringstream ss;
  ss << basepath << "-" << index << ".mpg";
  return ss.str();
}

struct FirstPhaseSetting {
  std::string videoBasePath;
  std::string audioFilePath;
};

struct FileAudioFrameInfo : public AudioFrameInfo {
  int audioIdx;
  int codedDataSize;
  int64_t fileOffset;

  FileAudioFrameInfo()
    : AudioFrameInfo()
    , audioIdx(0)
    , codedDataSize(0)
    , fileOffset(0)
  { }

  FileAudioFrameInfo(const AudioFrameInfo& info)
    : AudioFrameInfo(info)
    , audioIdx(0)
    , codedDataSize(0)
    , fileOffset(0)
  { }
};

enum StreamEventType {
  STREAM_EVENT_NONE = 0,
  PID_TABLE_CHANGED,
  VIDEO_FORMAT_CHANGED,
  AUDIO_FORMAT_CHANGED
};

struct StreamEvent {
  StreamEventType type;
  int frameIdx;  // �t���[���ԍ�
  int audioIdx;  // �ύX���ꂽ�����C���f�b�N�X�iAUDIO_FORMAT_CHANGED�̂Ƃ��̂ݗL���j
  int numAudio;  // �����̐��iPID_TABLE_CHANGED�̂Ƃ��̂ݗL���j
};

typedef std::vector<std::unique_ptr<std::vector<int>>> FileAudioFrameList;

struct OutVideoFormat {
  int formatId; // �����t�H�[�}�b�gID�i�ʂ��ԍ��j
  int videoFileId;
  VideoFormat videoFormat;
  std::vector<AudioFormat> audioFormat;
};

class StreamReformInfo {
public:
  StreamReformInfo(
    int numVideoFile,
    std::vector<VideoFrameInfo>& videoFrameList,
    std::vector<FileAudioFrameInfo>& audioFrameList,
    std::vector<StreamEvent>& streamEventList)
    : numVideoFile_(numVideoFile)
    , videoFrameList_(std::move(videoFrameList))
    , audioFrameList_(std::move(audioFrameList))
    , streamEventList_(std::move(streamEventList))
  {
    // TODO:
    encodedFrames_.resize(videoFrameList_.size(), false);
  }

  // PTS -> video frame index
  int getVideoFrameIndex(int64_t PTS, int videoFileIndex) const {
    auto it = framePtsMap_.find(PTS);
    if (it == framePtsMap_.end()) {
      return -1;
    }
    
    // TODO: check videoFileIndex

    return it->second;
  }

  int getNumEncoders(int videoFileIndex) const {
    return int(
      outFormatStartIndex_[videoFileIndex + 1] - outFormatStartIndex_[videoFileIndex]);
  }

  // video frame index -> VideoFrameInfo
  const VideoFrameInfo& getVideoFrameInfo(int frameIndex) const {
    return videoFrameList_[frameIndex];
  }

  // video frame index -> encoder index
  int getEncoderIndex(int frameIndex) const {
    int formatId = frameFormatId_[frameIndex];
    const auto& format = outFormat_[formatId];
    return formatId - outFormatStartIndex_[format.videoFileId];
  }

  // �t���[�����G���R�[�h�����t���O���Z�b�g
  void frameEncoded(int frameIndex) {
    encodedFrames_[frameIndex] = true;
  }

  const OutVideoFormat& getFormat(int encoderIndex, int videoFileIndex) const {
    int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
    return outFormat_[formatId];
  }

  void prepare3rdPhase() {
    // TODO:
  }

  const std::unique_ptr<FileAudioFrameList>& getFileAudioFrameList(
    int encoderIndex, int videoFileIndex)
  {
    int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
    return reformedAudioFrameList_[formatId];
  }

private:
  // 1st phase �o��
  int numVideoFile_;
  std::vector<VideoFrameInfo> videoFrameList_;
  std::vector<FileAudioFrameInfo> audioFrameList_;
  std::vector<StreamEvent> streamEventList_;

  // �v�Z�f�[�^
  std::vector<int64_t> modifiedPTS_; // ���b�v�A���E���h���Ȃ�PTS
  std::vector<int64_t> dataPTS_; // �f���t���[���̃X�g���[����ł̈ʒu��PTS�̊֘A�t��
  std::vector<int64_t> streamEventPTS_;

  std::vector<OutVideoFormat> outFormat_;
  // ���ԉf���t�@�C�����Ƃ̃t�H�[�}�b�g�J�n�C���f�b�N�X
  // �T�C�Y�͒��ԉf���t�@�C����+1
  std::vector<int> outFormatStartIndex_;

  // 2nd phase ����
  std::vector<int> frameFormatId_; // videoFrameList_�Ɠ����T�C�Y
  std::map<int64_t, int> framePtsMap_;

  // 2nd phase �o��
  std::vector<bool> encodedFrames_;

  // 3rd phase ����
  std::vector<std::unique_ptr<FileAudioFrameList>> reformedAudioFrameList_;

  void reformMain()
  {
    if (videoFrameList_.size() == 0) {
      THROW(FormatException, "�f���t���[����1��������܂���");
    }
    if (audioFrameList_.size() == 0) {
      THROW(FormatException, "�����t���[����1��������܂���");
    }
    if (streamEventList_.size() == 0 || streamEventList_[0].type != PID_TABLE_CHANGED) {
      THROW(FormatException, "�s���ȃf�[�^�ł�");
    }

    // framePtsMap_���쐬�i�����ɍ���̂Łj
    for (int i = 0; i < int(videoFrameList_.size()); ++i) {
      framePtsMap_[videoFrameList_[i].PTS] = i;
    }

    // ���b�v�A���E���h���Ȃ�PTS�𐶐�
    modifiedPTS_.reserve(videoFrameList_.size());
    int64_t prevPTS = videoFrameList_[0].PTS;
    for (int i = 0; i < int(videoFrameList_.size()); ++i) {
      int64_t PTS = videoFrameList_[i].PTS;
      int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
      modifiedPTS_.push_back(modPTS);
      prevPTS = PTS;
    }

    // �X�g���[�����߂��Ă���ꍇ�͏����ł��Ȃ��̂ŃG���[�Ƃ���
    for (int i = 1; i < int(videoFrameList_.size()); ++i) {
      if (modifiedPTS_[i] - modifiedPTS_[i - 1] < -60 * MPEG_CLOCK_HZ) {
        // 1���ȏ�߂��Ă�����G���[�Ƃ���
        THROWF(FormatException,
          "PTS���߂��Ă��܂��B�����ł��܂���B %llu -> %llu",
          modifiedPTS_[i - 1], modifiedPTS_[i]);
      }
    }

    // dataPTS�𐶐�
    // ��납�猩�Ă��̎��_�ōł�������PTS��dataPTS�Ƃ���
    int64_t curMin = INT64_MAX;
    int64_t curMax = 0;
    dataPTS_.resize(videoFrameList_.size());
    for (int i = (int)videoFrameList_.size() - 1; i >= 0; --i) {
      curMin = std::min(curMin, modifiedPTS_[i]);
      curMax = std::max(curMax, modifiedPTS_[i]);
      dataPTS_[i] = curMin;
    }

    // �X�g���[���C�x���g��PTS���v�Z
    int64_t exceedLastPTS = curMax + 1;
    streamEventPTS_.resize(streamEventList_.size());
    for (int i = 0; i < (int)streamEventList_.size(); ++i) {
      auto& ev = streamEventList_[i];
      int64_t pts = -1;
      if (ev.type == PID_TABLE_CHANGED || ev.type == VIDEO_FORMAT_CHANGED) {
        if (ev.frameIdx >= (int)videoFrameList_.size()) {
          // ���߂��đΏۂ̃t���[�����Ȃ�
          pts = exceedLastPTS;
        }
        else {
          pts = dataPTS_[ev.frameIdx];
        }
      }
      else if (ev.type == AUDIO_FORMAT_CHANGED) {
        if (ev.frameIdx >= (int)audioFrameList_.size()) {
          // ���߂��đΏۂ̃t���[�����Ȃ�
          pts = exceedLastPTS;
        }
        else {
          pts = audioFrameList_[ev.frameIdx].PTS;
        }
      }
      streamEventPTS_[i] = pts;
    }

    struct SingleFormatSection {
      int formatId;
      int64_t fromPTS, toPTS;
    };

    // ���ԓI�ɋ߂��X�g���[���C�x���g��1�̕ω��_�Ƃ݂Ȃ�
    const int64_t CHANGE_TORELANCE = 3 * MPEG_CLOCK_HZ;

    std::vector<SingleFormatSection> sectionList;

    OutVideoFormat curFormat = OutVideoFormat();
    SingleFormatSection curSection = SingleFormatSection();
    int64_t curFromPTS = -1;
    curFormat.videoFileId = -1;
    for (int i = 0; i < (int)streamEventList_.size(); ++i) {
      auto& ev = streamEventList_[i];
      int64_t pts = streamEventPTS_[i];
      if (pts >= exceedLastPTS) {
        // ���ɉf�����Ȃ���ΈӖ����Ȃ�
        continue;
      }
      if (curFromPTS == -1) { // �ŏ�
        curFromPTS = curSection.fromPTS = pts;
      }
      else if (curFromPTS + CHANGE_TORELANCE < pts) {
        // ��Ԃ�ǉ�
        curSection.toPTS = pts;
        registerOrGetFormat(curFormat);
        curSection.formatId = curFormat.formatId;
        sectionList.push_back(curSection);

        curFromPTS = curSection.fromPTS = pts;
      }
      // �ύX�𔽉f
      switch (ev.type) {
      case PID_TABLE_CHANGED:
        curFormat.audioFormat.resize(ev.numAudio);
        break;
      case VIDEO_FORMAT_CHANGED:
        // �t�@�C���ύX
        ++curFormat.videoFileId;
        outFormatStartIndex_.push_back(outFormat_.size());
        curFormat.videoFormat = videoFrameList_[ev.frameIdx].format;
        break;
      case AUDIO_FORMAT_CHANGED:
        if (curFormat.audioFormat.size() >= ev.audioIdx) {
          THROW(FormatException, "StreamEvent's audioIdx exceeds numAudio of the previous table change event");
        }
        curFormat.audioFormat[ev.audioIdx] = audioFrameList_[ev.audioIdx].format;
        break;
      }
    }
    // �Ō�̋�Ԃ�ǉ�
    curSection.toPTS = exceedLastPTS;
    registerOrGetFormat(curFormat);
    curSection.formatId = curFormat.formatId;
    sectionList.push_back(curSection);
    outFormatStartIndex_.push_back(outFormat_.size());

    // frameFormatId_�𐶐�
    frameFormatId_.resize(videoFrameList_.size());
    for (int i = 0; i < int(videoFrameList_.size()); ++i) {
      int64_t pts = modifiedPTS_[i];
      // ��Ԃ�T��
      int formatId = std::partition_point(sectionList.begin(), sectionList.end(),
        [=](const SingleFormatSection& sec) {
          return !(pts < sec.toPTS);
        })->formatId;
      frameFormatId_[i] = formatId;
    }
  }

  void registerOrGetFormat(OutVideoFormat& format) {
    // ���łɂ���̂���T��
    for (int i = outFormatStartIndex_.back(); i < (int)outFormat_.size(); ++i) {
      if (isEquealFormat(outFormat_[i], format)) {
        format.formatId = i;
        return;
      }
    }
    // �Ȃ��̂œo�^
    format.formatId = (int)outFormat_.size();
    outFormat_.push_back(format);
  }

  bool isEquealFormat(const OutVideoFormat& a, const OutVideoFormat& b) {
    if (a.videoFormat != b.videoFormat) return false;
    if (a.audioFormat.size() != b.audioFormat.size()) return false;
    for (int i = 0; i < (int)a.audioFormat.size(); ++i) {
      if (a.audioFormat[i] != b.audioFormat[i]) {
        return false;
      }
    }
    return true;
  }

  int getEncoderIdx(const OutVideoFormat& format) {
    return format.formatId - outFormatStartIndex_[format.videoFileId];
  }

  static int numGenerateFrames(const VideoFrameInfo& frameInfo) {
    // BFF RFF����2���A����1��
    return (frameInfo.pic == PIC_BFF_RFF) ? 2 : 1;
  }

  void genAudioStream() {
    // TODO: encodedFrames_���特���X�g���[���̃t���[����𐶐�
  }
};

class FirstPhaseConverter : public TsSplitter {
public:
  FirstPhaseConverter(TsSplitterContext *ctx, FirstPhaseSetting* setting)
    : TsSplitter(ctx)
    , setting_(setting)
    , psWriter(ctx)
    , writeHandler(*this)
    , audioFile_(setting->audioFilePath, "wb")
    , videoFileCount_(0)
    , videoStreamType_(-1)
    , audioStreamType_(-1)
    , audioFileSize_(0)
  {
    psWriter.setHandler(&writeHandler);
  }

  StreamReformInfo reformInfo() {
    
    // for debug
    printInteraceCount();

    return StreamReformInfo(videoFileCount_,
      videoFrameList_, audioFrameList_, streamEventList_);
  }

protected:
  class StreamFileWriteHandler : public PsStreamWriter::EventHandler {
    TsSplitter& this_;
    std::unique_ptr<File> file_;
  public:
    StreamFileWriteHandler(TsSplitter& this_)
      : this_(this_) { }
    virtual void onStreamData(MemoryChunk mc) {
      if (file_ != NULL) {
        file_->write(mc);
      }
    }
    void open(const std::string& path) {
      file_ = std::unique_ptr<File>(new File(path, "wb"));
    }
    void close() {
      file_ = nullptr;
    }
  };

  FirstPhaseSetting* setting_;
  PsStreamWriter psWriter;
  StreamFileWriteHandler writeHandler;
  File audioFile_;

  int videoFileCount_;
  int videoStreamType_;
  int audioStreamType_;
  int64_t audioFileSize_;

  // �f�[�^
  std::vector<VideoFrameInfo> videoFrameList_;
  std::vector<FileAudioFrameInfo> audioFrameList_;
  std::vector<StreamEvent> streamEventList_;


  static bool CheckPullDown(PICTURE_TYPE p0, PICTURE_TYPE p1) {
    switch (p0) {
    case PIC_TFF:
    case PIC_BFF_RFF:
      return (p1 == PIC_TFF || p1 == PIC_TFF_RFF);
    case PIC_BFF:
    case PIC_TFF_RFF:
      return (p1 == PIC_BFF || p1 == PIC_BFF_RFF);
    default: // ����ȊO�̓`�F�b�N�ΏۊO
      return true;
    }
  }

  void printInteraceCount() {

    if (videoFrameList_.size() == 0) {
      printf("�t���[��������܂���");
      return;
    }

    // ���b�v�A���E���h���Ȃ�PTS�𐶐�
    std::vector<std::pair<int64_t, int>> modifiedPTS;
    int64_t videoBasePTS = videoFrameList_[0].PTS;
    int64_t prevPTS = videoFrameList_[0].PTS;
    for (int i = 0; i < int(videoFrameList_.size()); ++i) {
      int64_t PTS = videoFrameList_[i].PTS;
      int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
      modifiedPTS.push_back(std::make_pair(modPTS, i));
      prevPTS = PTS;
    }

    // PTS�Ń\�[�g
    std::sort(modifiedPTS.begin(), modifiedPTS.end());

    // �t���[�����X�g���o��
    FILE* framesfp = fopen("frames.txt", "w");
    fprintf(framesfp, "FrameNumber,DecodeFrameNumber,PTS,Duration,FRAME_TYPE,PIC_TYPE,IsGOPStart\n");
    for (int i = 0; i < (int)modifiedPTS.size(); ++i) {
      int64_t PTS = modifiedPTS[i].first;
      int decodeIndex = modifiedPTS[i].second;
      const VideoFrameInfo& frame = videoFrameList_[decodeIndex];
      int PTSdiff = -1;
      if (i < (int)modifiedPTS.size() - 1) {
        int64_t nextPTS = modifiedPTS[i + 1].first;
        const VideoFrameInfo& nextFrame = videoFrameList_[modifiedPTS[i + 1].second];
        PTSdiff = int(nextPTS - PTS);
        if (CheckPullDown(frame.pic, nextFrame.pic) == false) {
          printf("Flag Check Error: PTS=%lld %s -> %s\n",
            PTS, PictureTypeString(frame.pic), PictureTypeString(nextFrame.pic));
        }
      }
      fprintf(framesfp, "%d,%d,%lld,%d,%s,%s,%d\n",
        i, decodeIndex, PTS, PTSdiff, FrameTypeString(frame.type), PictureTypeString(frame.pic), frame.isGopStart ? 1 : 0);
    }
    fclose(framesfp);

    // PTS�Ԋu���o��
    struct Integer {
      int v;
      Integer() : v(0) { }
    };

    std::array<int, MAX_PIC_TYPE> interaceCounter = { 0 };
    std::map<int, Integer> PTSdiffMap;
    prevPTS = -1;
    for (const auto& ptsIndex : modifiedPTS) {
      int64_t PTS = ptsIndex.first;
      const VideoFrameInfo& frame = videoFrameList_[ptsIndex.second];
      interaceCounter[(int)frame.pic]++;
      if (prevPTS != -1) {
        int PTSdiff = int(PTS - prevPTS);
        PTSdiffMap[PTSdiff].v++;
      }
      prevPTS = PTS;
    }

    int64_t totalTime = modifiedPTS.back().first - videoBasePTS;
    ctx->info("����: %f �b", totalTime / 90000.0);

    ctx->info("�t���[���J�E���^");
    ctx->info("FRAME=%d DBL=%d TLP=%d TFF=%d BFF=%d TFF_RFF=%d BFF_RFF=%d",
      interaceCounter[0], interaceCounter[1], interaceCounter[2], interaceCounter[3], interaceCounter[4], interaceCounter[5], interaceCounter[6]);

    for (const auto& pair : PTSdiffMap) {
      ctx->info("(PTS_Diff,Cnt)=(%d,%d)\n", pair.first, pair.second.v);
    }
  }

  // TsSplitter���z�֐� //

  virtual void onVideoPesPacket(
    int64_t clock,
    const std::vector<VideoFrameInfo>& frames,
    PESPacket packet)
  {
		for (const VideoFrameInfo& frame : frames) {
      videoFrameList_.push_back(frame);
    }
    psWriter.outVideoPesPacket(clock, frames, packet);
  }

  virtual void onVideoFormatChanged(VideoFormat fmt) {
    ctx->debug("�f���t�H�[�}�b�g�ύX�����m");
    ctx->debug("�T�C�Y: %dx%d FPS: %d/%d", fmt.width, fmt.height, fmt.frameRateNum, fmt.frameRateDenom);

    // �o�̓t�@�C����ύX
    writeHandler.open(makeIntVideoFilePath(setting_->videoBasePath, videoFileCount_++));
    psWriter.outHeader(videoStreamType_, audioStreamType_);

    StreamEvent ev = StreamEvent();
    ev.type = VIDEO_FORMAT_CHANGED;
    ev.frameIdx = (int)videoFrameList_.size();
    streamEventList_.push_back(ev);
  }

  virtual void onAudioPesPacket(
    int audioIdx, 
    int64_t clock, 
    const std::vector<AudioFrameData>& frames, 
    PESPacket packet)
  {
    MemoryChunk payload = packet.paylod();
    audioFile_.write(payload);

    int64_t offset = 0;
    for (const AudioFrameData& frame : frames) {
      FileAudioFrameInfo info = frame;
      info.audioIdx = audioIdx;
      info.codedDataSize = frame.codedDataSize;
      info.fileOffset = audioFileSize_ + offset;
      offset += frame.codedDataSize;
      audioFrameList_.push_back(info);
    }

    ASSERT(offset == payload.length);
    audioFileSize_ += payload.length;

    if (videoFileCount_ > 0) {
      psWriter.outAudioPesPacket(audioIdx, clock, frames, packet);
    }
  }

  virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) {
		ctx->debug("���� %d �̃t�H�[�}�b�g�ύX�����m", audioIdx);
    ctx->debug("�`�����l��: %s �T���v�����[�g: %d",
      getAudioChannelString(fmt.channels), fmt.sampleRate);

    StreamEvent ev = StreamEvent();
    ev.type = AUDIO_FORMAT_CHANGED;
    ev.audioIdx = audioIdx;
    ev.frameIdx = (int)audioFrameList_.size();
    streamEventList_.push_back(ev);
  }

  // TsPacketSelectorHandler���z�֐� //

  virtual void onPidTableChanged(const PMTESInfo video, const std::vector<PMTESInfo>& audio) {
    // �x�[�X�N���X�̏���
    TsSplitter::onPidTableChanged(video, audio);

    ASSERT(audio.size() > 0);
    videoStreamType_ = video.stype;
    audioStreamType_ = audio[0].stype;

    StreamEvent ev = StreamEvent();
    ev.type = PID_TABLE_CHANGED;
    ev.numAudio = (int)audio.size();
    ev.frameIdx = (int)videoFrameList_.size();
    streamEventList_.push_back(ev);
  }
};
