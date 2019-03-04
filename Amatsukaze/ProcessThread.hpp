/**
* Sub process and thread utility
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <Windows.h>
#include <process.h>

#include <deque>
#include <string>
#include <mutex>
#include <condition_variable>

#include "StreamUtils.hpp"
#include "PerformanceUtil.hpp"

// �X���b�h��start()�ŊJ�n�i�R���X�g���N�^���牼�z�֐����ĂԂ��Ƃ͂ł��Ȃ����߁j
// run()�͔h���N���X�Ŏ�������Ă���̂�run()���I������O�ɔh���N���X�̃f�X�g���N�^���I�����Ȃ��悤�ɒ��ӁI
// ���S�̂���join()���������Ă��Ȃ���Ԃ�ThreadBase�̃f�X�g���N�^�ɓ���ƃG���[�Ƃ���
class ThreadBase
{
public:
	ThreadBase() : thread_handle_(NULL) { }
	~ThreadBase() {
		if (thread_handle_ != NULL) {
			THROW(InvalidOperationException, "finish join() before destroy object ...");
		}
	}
	void start() {
		if (thread_handle_ != NULL) {
			THROW(InvalidOperationException, "thread already started ...");
		}
		thread_handle_ = (HANDLE)_beginthreadex(NULL, 0, thread_, this, 0, NULL);
		if (thread_handle_ == (HANDLE)-1) {
			THROW(RuntimeException, "failed to begin pump thread ...");
		}
	}
	void join() {
		if (thread_handle_ != NULL) {
			WaitForSingleObject(thread_handle_, INFINITE);
			CloseHandle(thread_handle_);
			thread_handle_ = NULL;
		}
	}
	bool isRunning() { return thread_handle_ != NULL; }

protected:
	virtual void run() = 0;

private:
	HANDLE thread_handle_;

	static unsigned __stdcall thread_(void* arg) {
		try {
			static_cast<ThreadBase*>(arg)->run();
		}
		catch (const Exception& e) {
			throw e;
		}
		return 0;
	}
};

template <typename T, bool PERF = false>
class DataPumpThread : private ThreadBase
{
public:
	DataPumpThread(size_t maximum)
		: maximum_(maximum)
		, current_(0)
		, finished_(false)
		, error_(false)
	{ }

	~DataPumpThread() {
		if (isRunning()) {
			THROW(InvalidOperationException, "call join() before destroy object ...");
		}
	}

	void put(T&& data, size_t amount)
	{
		std::unique_lock<std::mutex> lock(critical_section_);
		if (error_) {
			THROW(RuntimeException, "DataPumpThread error");
		}
		if (finished_) {
			THROW(InvalidOperationException, "DataPumpThread is already finished");
		}
		while (current_ >= maximum_) {
			if (PERF) producer.start();
			cond_full_.wait(lock);
			if (PERF) producer.stop();
		}
		if (data_.size() == 0) {
			cond_empty_.notify_one();
		}
		data_.emplace_back(amount, std::move(data));
		current_ += amount;
	}

	void start() {
		finished_ = false;
		producer.reset();
		consumer.reset();
		ThreadBase::start();
	}

	void join() {
		{
			std::unique_lock<std::mutex> lock(critical_section_);
			finished_ = true;
			cond_empty_.notify_one();
		}
		ThreadBase::join();
	}

	bool isRunning() { return ThreadBase::isRunning(); }

	void getTotalWait(double& prod, double& cons) {
		prod = producer.getTotal();
		cons = consumer.getTotal();
	}

protected:
	virtual void OnDataReceived(T&& data) = 0;

private:
	std::mutex critical_section_;
	std::condition_variable cond_full_;
	std::condition_variable cond_empty_;

	std::deque<std::pair<size_t, T>> data_;

	size_t maximum_;
	size_t current_;

	bool finished_;
	bool error_;

	Stopwatch producer;
	Stopwatch consumer;

	virtual void run()
	{
		while (true) {
			T data;
			{
				std::unique_lock<std::mutex> lock(critical_section_);
				while (data_.size() == 0) {
					// data_.size()==0��finished_�Ȃ�I��
					if (finished_ || error_) return;
					if (PERF) consumer.start();
					cond_empty_.wait(lock);
					if (PERF) consumer.stop();
				}
				auto& entry = data_.front();
				size_t newsize = current_ - entry.first;
				if ((current_ >= maximum_) && (newsize < maximum_)) {
					cond_full_.notify_all();
				}
				current_ = newsize;
				data = std::move(entry.second);
				data_.pop_front();
			}
			if (error_ == false) {
				try {
					OnDataReceived(std::move(data));
				}
				catch (Exception&) {
					error_ = true;
				}
			}
		}
	}
};

class SubProcess
{
public:
	SubProcess(const tstring& args)
	{
		STARTUPINFOW si = STARTUPINFOW();

		si.cb = sizeof(si);
		si.hStdError = stdErrPipe_.writeHandle;
		si.hStdOutput = stdOutPipe_.writeHandle;
		si.hStdInput = stdInPipe_.readHandle;
		si.dwFlags |= STARTF_USESTDHANDLES;

		// �K�v�Ȃ��n���h���͌p���𖳌���
		if (SetHandleInformation(stdErrPipe_.readHandle, HANDLE_FLAG_INHERIT, 0) == 0 ||
			SetHandleInformation(stdOutPipe_.readHandle, HANDLE_FLAG_INHERIT, 0) == 0 ||
			SetHandleInformation(stdInPipe_.writeHandle, HANDLE_FLAG_INHERIT, 0) == 0)
		{
			THROW(RuntimeException, "failed to set handle information");
		}

		// Priority Class�͖������Ă��Ȃ��̂ŁA�f�t�H���g����ƂȂ�
		// �f�t�H���g����́A�e�v���Z�X�i���̃v���Z�X�j��NORMAL_PRIORITY_CLASS�ȏ��
		// �����Ă���ꍇ�́ANORMAL_PRIORITY_CLASS
		// IDLE_PRIORITY_CLASS�����BELOW_NORMAL_PRIORITY_CLASS��
		// �����Ă���ꍇ�́A����Priority Class���p�������
		// Priority Class�͎q�v���Z�X�Ɍp�������Ώۂł͂Ȃ����A
		// NORMAL_PRIORITY_CLASS�ȉ��ł͎����p������邱�Ƃɒ���

		if (CreateProcessW(NULL, const_cast<tchar*>(args.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi_) == 0) {
			THROW(RuntimeException, "�v���Z�X�N���Ɏ��s�Bexe�̃p�X���m�F���Ă��������B");
		}

		// �q�v���Z�X�p�̃n���h���͕K�v�Ȃ��̂ŕ���
		stdErrPipe_.closeWrite();
		stdOutPipe_.closeWrite();
		stdInPipe_.closeRead();
	}
	~SubProcess() {
		join();
	}
	void write(MemoryChunk mc) {
		if (mc.length > 0xFFFFFFFF) {
			THROW(RuntimeException, "buffer too large");
		}
		DWORD bytesWritten = 0;
		if (WriteFile(stdInPipe_.writeHandle, mc.data, (DWORD)mc.length, &bytesWritten, NULL) == 0) {
			THROW(RuntimeException, "failed to write to stdin pipe");
		}
		if (bytesWritten != mc.length) {
			THROW(RuntimeException, "failed to write to stdin pipe (bytes written mismatch)");
		}
	}
	size_t readErr(MemoryChunk mc) {
		return readGeneric(mc, stdErrPipe_.readHandle);
	}
	size_t readOut(MemoryChunk mc) {
		return readGeneric(mc, stdOutPipe_.readHandle);
	}
	void finishWrite() {
		stdInPipe_.closeWrite();
	}
	int join() {
		if (pi_.hProcess != NULL) {
			// �q�v���Z�X�̏I����҂�
			WaitForSingleObject(pi_.hProcess, INFINITE);
			// �I���R�[�h�擾
			GetExitCodeProcess(pi_.hProcess, &exitCode_);

			CloseHandle(pi_.hProcess);
			CloseHandle(pi_.hThread);
			pi_.hProcess = NULL;
		}
		return exitCode_;
	}
private:
	class Pipe {
	public:
		Pipe() {
			// �p����L���ɂ��č쐬
			SECURITY_ATTRIBUTES sa = SECURITY_ATTRIBUTES();
			sa.nLength = sizeof(sa);
			sa.bInheritHandle = TRUE;
			sa.lpSecurityDescriptor = NULL;
			if (CreatePipe(&readHandle, &writeHandle, &sa, 0) == 0) {
				THROW(RuntimeException, "failed to create pipe");
			}
		}
		~Pipe() {
			closeRead();
			closeWrite();
		}
		void closeRead() {
			if (readHandle != NULL) {
				CloseHandle(readHandle);
				readHandle = NULL;
			}
		}
		void closeWrite() {
			if (writeHandle != NULL) {
				CloseHandle(writeHandle);
				writeHandle = NULL;
			}
		}
		HANDLE readHandle;
		HANDLE writeHandle;
	};

	PROCESS_INFORMATION pi_ = PROCESS_INFORMATION();
	Pipe stdErrPipe_;
	Pipe stdOutPipe_;
	Pipe stdInPipe_;
	DWORD exitCode_;

	size_t readGeneric(MemoryChunk mc, HANDLE readHandle)
	{
		if (mc.length > 0xFFFFFFFF) {
			THROW(RuntimeException, "buffer too large");
		}
		DWORD bytesRead = 0;
		while (true) {
			if (ReadFile(readHandle, mc.data, (DWORD)mc.length, &bytesRead, NULL) == 0) {
				if (GetLastError() == ERROR_BROKEN_PIPE) {
					return 0;
				}
				THROW(RuntimeException, "failed to read from pipe");
			}
			// �p�C�v��WriteFile�Ƀ[����n����ReadFile���[���ŋA���Ă���̂Ń`�F�b�N
			if (bytesRead != 0) {
				break;
			}
		}
		return bytesRead;
	}
};

class EventBaseSubProcess : public SubProcess
{
public:
	EventBaseSubProcess(const tstring& args)
		: SubProcess(args)
		, drainOut(this, false)
		, drainErr(this, true)
	{
		drainOut.start();
		drainErr.start();
	}
	~EventBaseSubProcess() {
		if (drainOut.isRunning()) {
			THROW(InvalidOperationException, "call join before destroy object ...");
		}
	}
	int join() {
		/*
		* �I�������̗���
		* finishWrite()
		* -> �q�v���Z�X���I�����m
		* -> �q�v���Z�X���I��
		* -> stdout,stderr�̏������݃n���h���������I�ɕ���
		* -> SubProcess.readGeneric()��EOFException��Ԃ�
		* -> DrainThread����O���L���b�`���ďI��
		* -> DrainThread��join()������
		* -> EventBaseSubProcess��join()������
		* -> �v���Z�X�͏I�����Ă���̂�SubProcess�̃f�X�g���N�^�͂����Ɋ���
		*/
		try {
			finishWrite();
		}
		catch (RuntimeException&) {
			// �q�v���Z�X���G���[�I�����Ă���Ə������݂Ɏ��s���邪��������
		}
		drainOut.join();
		drainErr.join();
		return SubProcess::join();
	}
	bool isRunning() { return drainOut.isRunning(); }
protected:
	virtual void onOut(bool isErr, MemoryChunk mc) = 0;

private:
	class DrainThread : public ThreadBase {
	public:
		DrainThread(EventBaseSubProcess* this_, bool isErr)
			: this_(this_)
			, isErr_(isErr)
		{ }
		virtual void run() {
			this_->drain_thread(isErr_);
		}
	private:
		EventBaseSubProcess* this_;
		bool isErr_;
	};

	DrainThread drainOut;
	DrainThread drainErr;

	void drain_thread(bool isErr) {
		std::vector<uint8_t> buffer(4 * 1024);
		MemoryChunk mc(buffer.data(), buffer.size());
		while (true) {
			size_t bytesRead = isErr ? readErr(mc) : readOut(mc);
			if (bytesRead == 0) { // �I��
				break;
			}
			onOut(isErr, MemoryChunk(mc.data, bytesRead));
		}
	}
};

class StdRedirectedSubProcess : public EventBaseSubProcess
{
public:
	StdRedirectedSubProcess(const tstring& args, int bufferLines = 0, bool isUtf8 = false)
		: EventBaseSubProcess(args)
		, bufferLines(bufferLines)
		, isUtf8(isUtf8)
		, outLiner(this, false)
		, errLiner(this, true)
	{ }

	~StdRedirectedSubProcess() {
		if (isUtf8) {
			outLiner.Flush();
			errLiner.Flush();
		}
	}

	const std::deque<std::vector<char>>& getLastLines() {
		outLiner.Flush();
		errLiner.Flush();
		return lastLines;
	}

private:
	class SpStringLiner : public StringLiner
	{
		StdRedirectedSubProcess* pThis;
	public:
		SpStringLiner(StdRedirectedSubProcess* pThis, bool isErr)
			: pThis(pThis), isErr(isErr) { }
	protected:
		bool isErr;
		virtual void OnTextLine(const uint8_t* ptr, int len, int brlen) {
			pThis->onTextLine(isErr, ptr, len, brlen);
		}
	};

	bool isUtf8;
	int bufferLines;
	SpStringLiner outLiner, errLiner;

	std::mutex mtx;
	std::deque<std::vector<char>> lastLines;

	void onTextLine(bool isErr, const uint8_t* ptr, int len, int brlen) {

		std::vector<char> line;
		if (isUtf8) {
			line = utf8ToString(ptr, len);
			// �ϊ�����ꍇ�͂����ŏo��
			auto out = isErr ? stderr : stdout;
			fwrite(line.data(), line.size(), 1, out);
			fprintf(out, "\n");
			fflush(out);
		}
		else {
			line = std::vector<char>(ptr, ptr + len);
		}

		if (bufferLines > 0) {
			std::lock_guard<std::mutex> lock(mtx);
			if (lastLines.size() > bufferLines) {
				lastLines.pop_front();
			}
			lastLines.push_back(line);
		}
	}

	virtual void onOut(bool isErr, MemoryChunk mc) {
		if (bufferLines > 0 || isUtf8) { // �K�v������ꍇ�̂�
			(isErr ? errLiner : outLiner).AddBytes(mc);
		}
		if (!isUtf8) {
			// �ϊ����Ȃ��ꍇ�͂����ł����ɏo��
			fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
			fflush(isErr ? stderr : stdout);
		}
	}
};

enum PROCESSOR_INFO_TAG {
	PROC_TAG_NONE = 0,
	PROC_TAG_CORE,
	PROC_TAG_L2,
	PROC_TAG_L3,
	PROC_TAG_NUMA,
	PROC_TAG_GROUP,
	PROC_TAG_COUNT
};

class CPUInfo
{
	std::vector<GROUP_AFFINITY> data[PROC_TAG_COUNT];
public:
	CPUInfo() {
		DWORD length = 0;
		GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			THROW(RuntimeException, "GetLogicalProcessorInformationEx��ERROR_INSUFFICIENT_BUFFER��Ԃ��Ȃ�����");
		}
		std::unique_ptr<uint8_t[]> buf = std::unique_ptr<uint8_t[]>(new uint8_t[length]);
		uint8_t* ptr = buf.get();
		uint8_t* end = ptr + length;
		if (GetLogicalProcessorInformationEx(
			RelationAll, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)ptr, &length) == 0) {
			THROW(RuntimeException, "GetLogicalProcessorInformationEx�Ɏ��s");
		}
		while (ptr < end) {
			auto info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)ptr;
			switch (info->Relationship) {
			case RelationCache:
				// �K�v�Ȃ̂�L2,L3�̂�
				if (info->Cache.Level == 2 || info->Cache.Level == 3) {
					data[(info->Cache.Level == 2) ? PROC_TAG_L2 : PROC_TAG_L3].push_back(info->Cache.GroupMask);
				}
				break;
			case RelationGroup:
				for (int i = 0; i < info->Group.ActiveGroupCount; ++i) {
					GROUP_AFFINITY af = GROUP_AFFINITY();
					af.Group = i;
					af.Mask = info->Group.GroupInfo[i].ActiveProcessorMask;
					data[PROC_TAG_GROUP].push_back(af);
				}
				break;
			case RelationNumaNode:
				data[PROC_TAG_NUMA].push_back(info->NumaNode.GroupMask);
				break;
			case RelationProcessorCore:
				if (info->Processor.GroupCount != 1) {
					THROW(RuntimeException, "GetLogicalProcessorInformationEx�ŗ\�����Ȃ��f�[�^");
				}
				data[PROC_TAG_CORE].push_back(info->Processor.GroupMask[0]);
				break;
			default:
				break;
			}
			ptr += info->Size;
		}
	}
	const GROUP_AFFINITY* GetData(PROCESSOR_INFO_TAG tag, int* count) {
		*count = (int)data[tag].size();
		return data[tag].data();
	}
};

extern "C" __declspec(dllexport) void* CPUInfo_Create(AMTContext* ctx) {
	try {
		return new CPUInfo();
	}
	catch (const Exception& exception) {
		ctx->setError(exception);
	}
	return nullptr;
}
extern "C" __declspec(dllexport) void CPUInfo_Delete(CPUInfo* ptr) { delete ptr; }
extern "C" __declspec(dllexport) const GROUP_AFFINITY* CPUInfo_GetData(CPUInfo* ptr, int tag, int* count)
{
	return ptr->GetData((PROCESSOR_INFO_TAG)tag, count);
}

bool SetCPUAffinity(int group, uint64_t mask)
{
	if (mask == 0) {
		return true;
	}
	GROUP_AFFINITY gf = GROUP_AFFINITY();
	gf.Group = group;
	gf.Mask = (KAFFINITY)mask;
	bool result = (SetThreadGroupAffinity(GetCurrentThread(), &gf, nullptr) != FALSE);
	// �v���Z�X�������̃O���[�v�ɂ܂������Ă�Ɓ��̓G���[�ɂȂ�炵��
	SetProcessAffinityMask(GetCurrentProcess(), (DWORD_PTR)mask);
	return result;
}
