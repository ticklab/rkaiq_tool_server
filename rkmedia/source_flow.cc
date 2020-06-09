// Copyright 2019 Fuzhou Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <sys/prctl.h>

#include "buffer.h"
#include "flow.h"
#include "stream.h"
#include "utils.h"

namespace easymedia {

class SourceFlow : public Flow {
public:
  SourceFlow(const char *param);
  virtual ~SourceFlow();
  static const char *GetFlowName() { return "source_flow"; }
  virtual int Control(unsigned long int request, ...) final {
    if (!stream)
      return -1;
    va_list vl;
    va_start(vl, request);
    void *arg = va_arg(vl, void *);
    va_end(vl);
    return stream->IoCtrl(request, arg);
  }

private:
  void ReadThreadRun();
  bool loop;
  std::thread *read_thread;
  std::shared_ptr<Stream> stream;
  std::string tag;
};

SourceFlow::SourceFlow(const char *param)
    : loop(false), read_thread(nullptr) {
  std::list<std::string> separate_list;
  std::map<std::string, std::string> params;
  if (!ParseWrapFlowParams(param, params, separate_list)) {
    SetError(-EINVAL);
    return;
  }
  std::string &name = params[KEY_NAME];
  const char *stream_name = name.c_str();
  const std::string &stream_param = separate_list.back();
  stream = REFLECTOR(Stream)::Create<Stream>(stream_name, stream_param.c_str());
  if (!stream) {
    LOG("Create stream %s failed\n", stream_name);
    SetError(-EINVAL);
    return;
  }
  tag = "SourceFlow:";
  tag.append(name);
  if (!SetAsSource(std::vector<int>({0}), void_transaction00, tag)) {
    SetError(-EINVAL);
    return;
  }
  loop = true;
  read_thread = new std::thread(&SourceFlow::ReadThreadRun, this);
  if (!read_thread) {
    loop = false;
    SetError(-EINVAL);
    return;
  }
  SetFlowTag(tag);
}

SourceFlow::~SourceFlow() {
  loop = false;
  StopAllThread();
  int stop = 1;
  if (stream && Control(S_STREAM_OFF, &stop))
    LOG("Fail to stop source stream\n");
  if (read_thread) {
    source_start_cond_mtx->lock();
    loop = false;
    source_start_cond_mtx->notify();
    source_start_cond_mtx->unlock();
    read_thread->join();
    delete read_thread;
  }
  stream.reset();
}

void SourceFlow::ReadThreadRun() {
  prctl(PR_SET_NAME, this->tag.c_str());
  source_start_cond_mtx->lock();
  if (down_flow_num == 0 && IsEnable())
    source_start_cond_mtx->wait();
  source_start_cond_mtx->unlock();
  while (loop) {
    if (stream->Eof()) {
      // TODO: tell that I reach eof
      SetDisable();
      break;
    }
    auto buffer = stream->Read();
    SendInput(buffer, 0);
  }
}

DEFINE_FLOW_FACTORY(SourceFlow, Flow)
const char *FACTORY(SourceFlow)::ExpectedInputDataType() {
  return nullptr;
}

// TODO!
const char *FACTORY(SourceFlow)::OutPutDataType() { return ""; }

} // namespace easymedia