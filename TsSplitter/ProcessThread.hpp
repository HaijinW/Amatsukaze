#pragma once

#include <Windows.h>
#include <process.h>

#include <deque>
#include <string>

#include "StreamUtils.hpp"

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
  bool isRunning() { return thread_handle_ != NULL;  }

protected:
  virtual void run() = 0;

private:
  HANDLE thread_handle_;

  static unsigned __stdcall thread_(void* arg) {
    static_cast<ThreadBase*>(arg)->run();
    return 0;
  }
};

template <typename T>
class DataPumpThread : private ThreadBase
{
public:
  DataPumpThread(size_t maximum)
    : maximum_(maximum)
    , current_(0)
    , finished_(false)
  { }

  ~DataPumpThread() {
    if (isRunning()) {
      THROW(InvalidOperationException, "call join() before destroy object ...");
    }
  }

  void put(T&& data, size_t amount)
  {
    auto& lock = with(critical_section_);
    while (current_ >= maximum_) {
      cond_full_.wait(critical_section_);
    }
    if (data_.size() == 0) {
      cond_empty_.signal();
    }
    data_.emplace_back(amount, std::move(data));
    current_ += amount;
  }

  void start() {
    ThreadBase::start();
  }

  void join() {
    {
      auto& lock = with(critical_section_);
      finished_ = true;
      cond_empty_.signal();
    }
    ThreadBase::join();
  }

  bool isRunning() { return ThreadBase::isRunning(); }

protected:
  virtual void OnDataReceived(T&& data) = 0;

private:
  CriticalSection critical_section_;
  CondWait cond_full_;
  CondWait cond_empty_;

  std::deque<std::pair<size_t, T>> data_;

  size_t maximum_;
  size_t current_;

  bool finished_;

  virtual void run()
  {
    while (true) {
      T data;
      {
        auto& lock = with(critical_section_);
        while (data_.size() == 0) {
          if (finished_) return;
          cond_empty_.wait(critical_section_);
          if (finished_) return;
        }
        auto& entry = data_.front();
        size_t newsize = current_ - entry.first;
        if ((current_ >= maximum_) && (newsize < maximum_)) {
          cond_full_.broadcast();
        }
        current_ = newsize;
        data = std::move(entry.second);
        data_.pop_front();
      }
      OnDataReceived(std::move(data));
    }
  }
};

class SubProcess
{
public:
  SubProcess(const std::string& args)
  {
    STARTUPINFO si = STARTUPINFO();

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

    if (CreateProcess(NULL, const_cast<char*>(args.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi_) == 0) {
      THROW(RuntimeException, "failed to create process");
    }

    // �q�v���Z�X�p�̃n���h���͕K�v�Ȃ��̂ŕ���
    stdErrPipe_.closeWrite();
    stdOutPipe_.closeWrite();
    stdInPipe_.closeRead();
  }
  ~SubProcess() {
    // �q�v���Z�X�̏I����҂�
    WaitForSingleObject(pi_.hProcess, INFINITE);

    CloseHandle(pi_.hProcess);
    CloseHandle(pi_.hThread);
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

  size_t readGeneric(MemoryChunk mc, HANDLE readHandle)
  {
    if (mc.length > 0xFFFFFFFF) {
      THROW(RuntimeException, "buffer too large");
    }
    DWORD bytesRead = 0;
    if (ReadFile(readHandle, mc.data, (DWORD)mc.length, &bytesRead, NULL) == 0) {
      if (GetLastError() == ERROR_BROKEN_PIPE) {
        throw EOFException("Pipe write handle is closed");
      }
      THROW(RuntimeException, "failed to read from pipe");
    }
    return bytesRead;
  }
};

class EventBaseSubProcess : public SubProcess
{
public:
  EventBaseSubProcess(const std::string& args)
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
  void join() {
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
    finishWrite();
    drainOut.join();
    drainErr.join();
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
    try {
      std::vector<uint8_t> buffer(4 * 1024);
      MemoryChunk mc(buffer.data(), buffer.size());
      while (true) {
        size_t bytesRead = isErr ? readErr(mc) : readOut(mc);
        onOut(isErr, MemoryChunk(mc.data, bytesRead));
      }
    }
    catch (EOFException) {
      // �I����
      return;
    }
  }
};
