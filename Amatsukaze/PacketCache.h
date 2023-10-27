#pragma once

/**
* Packet Cache for file
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <deque>
#include <vector>

#include "StreamUtils.h"

class PacketCache : public AMTObject {
public:
    PacketCache(
        AMTContext& ctx,
        const tstring& filepath,
        const std::vector<int64_t> offsets, // �f�[�^��+1�v�f
        int nLinebit, // �L���b�V�����C���f�[�^���̃r�b�g��
        int nEntry)	 // �ő�L���b�V���ێ����C����
;
    ~PacketCache();
    // MemoryChunk�͏��Ȃ��Ƃ�nEntry��̌Ăяo���܂ŗL��
    MemoryChunk operator[](int index);
private:
    int nLinebit_;
    int nEntry_;
    int nLineSize_;
    int nBaseIndexMask_;
    File file_;
    std::vector<int64_t> offsets_;

    std::vector<uint8_t*> cacheTable_;
    std::deque<int> cacheEntries_;

    int getLineNumber(int index) const;
    int getLineBaseIndex(int index) const;
    uint8_t* getEntry(int lineNumber);
};



