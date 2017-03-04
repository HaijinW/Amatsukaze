#pragma once

#include <string>
#include <sstream>

#include "StreamUtils.hpp"

// �J���[�X�y�[�X��`���g������
#include "libavutil/pixfmt.h"

namespace av {

// �J���[�X�y�[�X3�Z�b�g
// x265�͐��l���̂܂܂ł�OK�����Ax264��help���������string�łȂ����
// �Ȃ�Ȃ��悤�Ȃ̂ŕϊ����`
// �Ƃ肠����ARIB STD-B32 v3.7�ɏ����Ă���̂���

// 3���F
const char* getColorPrimStr(int color_prim) {
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
const char* getTransferCharacteristicsStr(int transfer_characteritics) {
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
const char* getColorSpaceStr(int color_space) {
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

std::string makeArgs(
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
