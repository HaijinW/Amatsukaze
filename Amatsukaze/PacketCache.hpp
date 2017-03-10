#pragma once

#include <deque>
#include <vector>

#include "StreamUtils.hpp"

class PacketCache : public AMTObject {
public:
	PacketCache(
		AMTContext& ctx, 
		const std::string& filepath, 
		const std::vector<int64_t> offsets, // �f�[�^��+1�v�f
		int nLinebit, // �L���b�V�����C���f�[�^���̃r�b�g��
		int nEntry)	 // �ő�L���b�V���ێ����C����
		: AMTObject(ctx)
		, file_(filepath, "rb")
		, offsets_(offsets)
		, nLinebit_(nLinebit)
		, nEntry_(nEntry)
	{
		int numData = (int)offsets.size() - 1;

		nLineSize_ = 1 << nLinebit;
		nBaseIndexMask_ = ~(nLineSize_ - 1);
		cacheTable_.resize((numData + nLineSize_ - 1) >> nLinebit_, nullptr);
	}
	~PacketCache() {
		for (int entry : cacheEntries_) {
			delete[] cacheTable_[entry];
		}
		cacheTable_.clear();
		cacheEntries_.clear();
	}
	// MemoryChunk�͏��Ȃ��Ƃ�nEntry��̌Ăяo���܂ŗL��
	MemoryChunk operator[](int index) {
		int64_t localOffset = offsets_[index] - offsets_[getLineBaseIndex(index)];
		int dataSize = int(offsets_[index + 1] - offsets_[index]);
		uint8_t* entryPtr = getEntry(getLineNumber(index));
		return MemoryChunk(entryPtr + localOffset, dataSize);
	}
private: 
	int nLinebit_;
	int nEntry_;
	int nLineSize_;
	int nBaseIndexMask_;
	File file_;
	std::vector<int64_t> offsets_;

	std::vector<uint8_t*> cacheTable_;
	std::deque<int> cacheEntries_;

	int getLineNumber(int index) const {
		return index >> nLinebit_;
	}
	int getLineBaseIndex(int index) const {
		return index & nBaseIndexMask_;
	}
	uint8_t* getEntry(int lineNumber) {
		uint8_t*& entry = cacheTable_[lineNumber];
		if (entry == nullptr) {
			// �L���b�V�����Ă��Ȃ��̂œǂݍ���
			if (cacheEntries_.size() >= nEntry_) {
				// �G���g�����𒴂���ꍇ�͍ŏ��ɓǂݍ��񂾃L���b�V�����C�����폜
				auto& firstEntry = cacheTable_[cacheEntries_.front()];
				delete[] firstEntry; firstEntry = nullptr;
				cacheEntries_.pop_front();
			}
			int baseIndex = lineNumber << nLinebit_;
			int numData = (int)offsets_.size() - 1;
			int64_t offset = offsets_[baseIndex];
			int64_t lineDataSize = offsets_[std::min(baseIndex + nLineSize_, numData)] - offset;
			entry = new uint8_t[lineDataSize];
			cacheEntries_.push_back(lineNumber);
			file_.seek(offset, SEEK_SET);
			file_.read(MemoryChunk(entry, lineDataSize));
		}
		return entry;
	}
};


