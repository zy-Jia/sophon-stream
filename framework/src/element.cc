#include "element.h"

#include <sys/prctl.h>

#include <iostream>
#include <nlohmann/json.hpp>

#include "common/logger.h"

namespace sophon_stream {
namespace framework {

constexpr const char* Element::JSON_ID_FIELD;
constexpr const char* Element::JSON_SIDE_FIELD;
constexpr const char* Element::JSON_DEVICE_ID_FIELD;
constexpr const char* Element::JSON_THREAD_NUMBER_FIELD;
constexpr const char* Element::JSON_MILLISECONDS_TIMEOUT_FIELD;
constexpr const char* Element::JSON_REPEATED_TIMEOUT_FIELD;
constexpr const char* Element::JSON_CONFIGURE_FIELD;
constexpr const char* Element::JSON_IS_SINK_FILED;
constexpr const int Element::DEFAULT_MILLISECONDS_TIMEOUT;

void Element::connect(Element& srcElement, int srcElementPort,
                      Element& dstElement, int dstElementPort) {
  auto& inputDataPipe = dstElement.mInputDataPipeMap[dstElementPort];
  if (!inputDataPipe) {
    inputDataPipe = std::make_shared<framework::DataPipe>();
    inputDataPipe->setPushHandler(
        std::bind(&Element::onInputNotify, &dstElement));
  }
  dstElement.addInputPort(dstElementPort);
  srcElement.addOutputPort(srcElementPort);
  srcElement.mOutputDataPipeMap[srcElementPort] = inputDataPipe;
}

Element::Element()
    : mId(-1),
      mDeviceId(-1),
      mThreadNumber(1),
      mThreadStatus(ThreadStatus::STOP),
      mNotifyCount(0),
      mMillisecondsTimeout(0),
      mRepeatedTimeout(false) {}

Element::~Element() {
  // stop();
}

common::ErrorCode Element::init(const std::string& json) {
  IVS_INFO("Init start, json: {0}", json);

  common::ErrorCode errorCode = common::ErrorCode::SUCCESS;

  do {
    auto configure = nlohmann::json::parse(json, nullptr, false);
    if (!configure.is_object()) {
      IVS_ERROR("Parse json fail or json is not object, json: {0}", json);
      errorCode = common::ErrorCode::PARSE_CONFIGURE_FAIL;
      break;
    }

    auto idIt = configure.find(JSON_ID_FIELD);
    if (configure.end() == idIt || !idIt->is_number_integer()) {
      IVS_ERROR(
          "Can not find {0} with integer type in element json configure, json: "
          "{1}",
          JSON_ID_FIELD, json);
      errorCode = common::ErrorCode::PARSE_CONFIGURE_FAIL;
      break;
    }

    mId = idIt->get<int>();

    auto sideIt = configure.find(JSON_SIDE_FIELD);
    if (configure.end() != sideIt && sideIt->is_string()) {
      mSide = sideIt->get<std::string>();
    }

    auto sinkIt = configure.find(JSON_IS_SINK_FILED);
    if(configure.end() != sinkIt && sinkIt->is_boolean()) {
        mLastElementFlag = sinkIt->get<bool>();
    }

    auto deviceIdIt = configure.find(JSON_DEVICE_ID_FIELD);
    if (configure.end() != deviceIdIt && deviceIdIt->is_number_integer()) {
      mDeviceId = deviceIdIt->get<int>();
    }

    auto threadNumberIt = configure.find(JSON_THREAD_NUMBER_FIELD);
    if (configure.end() != threadNumberIt &&
        threadNumberIt->is_number_integer()) {
      mThreadNumber = threadNumberIt->get<int>();
    }

    auto millisecondsTimeoutIt =
        configure.find(JSON_MILLISECONDS_TIMEOUT_FIELD);
    if (configure.end() != millisecondsTimeoutIt &&
        millisecondsTimeoutIt->is_number_integer()) {
      mMillisecondsTimeout = millisecondsTimeoutIt->get<int>();
    }

    auto repeatedTimeoutIt = configure.find(JSON_REPEATED_TIMEOUT_FIELD);
    if (configure.end() != repeatedTimeoutIt &&
        repeatedTimeoutIt->is_boolean()) {
      mRepeatedTimeout = repeatedTimeoutIt->get<bool>();
    }

    std::string internalConfigure;
    auto internalConfigureIt = configure.find(JSON_CONFIGURE_FIELD);
    if (configure.end() != internalConfigureIt) {
      internalConfigure = internalConfigureIt->dump();
    }

    errorCode = initInternal(internalConfigure);
    if (common::ErrorCode::SUCCESS != errorCode) {
      IVS_ERROR("Init internal fail, json: {0}", internalConfigure);
      break;
    }

  } while (false);

  if (common::ErrorCode::SUCCESS != errorCode) {
    uninit();
  }

  IVS_INFO("Init finish, json: {0}", json);
  return errorCode;
}

void Element::uninit() {
  int id = mId;
  IVS_INFO("Uninit start, element id: {0:d}", id);

  // stop();

  uninitInternal();

  mId = -1;
  mDeviceId = -1;
  mThreadNumber = 1;
  mMillisecondsTimeout = 0;
  mRepeatedTimeout = false;

  IVS_INFO("Uninit finish, element id: {0:d}", id);
}

common::ErrorCode Element::start() {
  IVS_INFO("Start element thread start, element id: {0:d}", mId);

  if (ThreadStatus::STOP != mThreadStatus) {
    IVS_ERROR("Can not start, current thread status is not stop");
    return common::ErrorCode::THREAD_STATUS_ERROR;
  }

  mThreadStatus = ThreadStatus::RUN;

  mThreads.reserve(mThreadNumber);
  for (int i = 0; i < mThreadNumber; ++i) {
    mThreads.push_back(
        std::make_shared<std::thread>(std::bind(&Element::run, this)));
  }

  IVS_INFO("Start element thread finish, element id: {0:d}", mId);
  return common::ErrorCode::SUCCESS;
}

common::ErrorCode Element::stop() {
  IVS_INFO("Stop element thread start, element id: {0:d}", mId);

  if (ThreadStatus::STOP == mThreadStatus) {
    IVS_ERROR("Can not stop, current thread status is stop");
    return common::ErrorCode::THREAD_STATUS_ERROR;
  }

  mThreadStatus = ThreadStatus::STOP;

  for (auto thread : mThreads) {
    thread->join();
  }
  mThreads.clear();

  IVS_INFO("Stop element thread finish, element id: {0:d}", mId);
  return common::ErrorCode::SUCCESS;
}

common::ErrorCode Element::pause() {
  IVS_INFO("Pause element thread start, element id: {0:d}", mId);

  if (ThreadStatus::RUN != mThreadStatus) {
    IVS_ERROR("Can not pause, current thread status is not run");
    return common::ErrorCode::THREAD_STATUS_ERROR;
  }

  mThreadStatus = ThreadStatus::PAUSE;

  IVS_INFO("Pause element thread finish, element id: {0:d}", mId);
  return common::ErrorCode::SUCCESS;
}

common::ErrorCode Element::resume() {
  IVS_INFO("Resume element thread start, element id: {0:d}", mId);

  if (ThreadStatus::PAUSE != mThreadStatus) {
    IVS_ERROR("Can not resume, current thread status is not pause");
    return common::ErrorCode::THREAD_STATUS_ERROR;
  }

  mThreadStatus = ThreadStatus::RUN;

  IVS_INFO("Resume element thread finish, element id: {0:d}", mId);
  return common::ErrorCode::SUCCESS;
}

void Element::run() {
  onStart();
  prctl(PR_SET_NAME, std::to_string(mId).c_str());
  {
    bool currentNoTimeout = true;
    bool lastNoTimeout = true;
    while (ThreadStatus::STOP != mThreadStatus) {
      std::chrono::milliseconds millisecondsTimeout(
          DEFAULT_MILLISECONDS_TIMEOUT);
      if (ThreadStatus::PAUSE != mThreadStatus && 0 != mMillisecondsTimeout) {
        millisecondsTimeout = std::chrono::milliseconds(mMillisecondsTimeout);
      }

      lastNoTimeout = currentNoTimeout;
      {
        std::unique_lock<std::mutex> lock(mMutex);

        currentNoTimeout = mCond.wait_for(
            lock, millisecondsTimeout, [this]() { return mNotifyCount > 0; });
      }

      if (ThreadStatus::PAUSE == mThreadStatus ||
          (!currentNoTimeout && (0 == mMillisecondsTimeout ||
                                 (!mRepeatedTimeout && !lastNoTimeout)))) {
        // 上一次timeout: continue, 上一次noTimeout: dowork
        // std::cout<<static_cast<int>(mThreadStatus.load())<<"--"
        // <<currentNoTimeout<<"--"
        // <<mMillisecondsTimeout
        // <<"--"<<mRepeatedTimeout<<"--"<<lastNoTimeout<<std::endl;
        continue;
      }
      if (currentNoTimeout) doWork();
    }
  }

  onStop();
}

common::ErrorCode Element::pushInputData(
    int inputPort, std::shared_ptr<void> data,
    const std::chrono::milliseconds& timeout) {
  IVS_DEBUG("push data, element id: {0:d}, input port: {1:d}, data: {2:p}", mId,
            inputPort, data.get());

  auto& inputDataPipe = mInputDataPipeMap[inputPort];
  if (!inputDataPipe) {
    inputDataPipe = std::make_shared<framework::DataPipe>();
    inputDataPipe->setPushHandler(std::bind(&Element::onInputNotify, this));
  }

  return inputDataPipe->pushData(data, timeout);
}

std::shared_ptr<void> Element::getInputData(int inputPort) const {
  auto dataPipeIt = mInputDataPipeMap.find(inputPort);
  if (mInputDataPipeMap.end() == dataPipeIt) {
    return std::shared_ptr<void>();
  }

  auto inputDataPipe = dataPipeIt->second;
  if (!inputDataPipe) {
    return std::shared_ptr<void>();
  }

  return inputDataPipe->getData();
}

void Element::popInputData(int inputPort) {
  IVS_DEBUG("pop data, element id: {0:d}, input port: {1:d}", mId, inputPort);

  auto dataPipeIt = mInputDataPipeMap.find(inputPort);
  if (mInputDataPipeMap.end() == dataPipeIt) {
    return;
  }

  auto inputDataPipe = dataPipeIt->second;
  if (!inputDataPipe) {
    return;
  }

  inputDataPipe->popData();
  mNotifyCount--;
}

void Element::setStopHandler(int outputPort, DataHandler dataHandler) {
  IVS_INFO("Set data handler, element id: {0:d}, output port: {1:d}", mId,
           outputPort);

  mStopHandlerMap[outputPort] = dataHandler;
}

common::ErrorCode Element::pushOutputData(
    int outputPort, std::shared_ptr<void> data,
    const std::chrono::milliseconds& timeout) {
  IVS_DEBUG("send data, element id: {0:d}, output port: {1:d}, data:{2:p}", mId,
            outputPort, data.get());
  // if (mLastElementFlag) {
  auto handlerIt = mStopHandlerMap.find(outputPort);
  if (mStopHandlerMap.end() != handlerIt) {
    auto dataHandler = handlerIt->second;
    if (dataHandler) {
      dataHandler(data);
      return common::ErrorCode::SUCCESS;
    }
  }
  // }

  auto dataPipeIt = mOutputDataPipeMap.find(outputPort);
  if (mOutputDataPipeMap.end() != dataPipeIt) {
    auto outputDataPipe = dataPipeIt->second.lock();
    if (outputDataPipe) {
      return outputDataPipe->pushData(data, timeout);
    }
  }

  IVS_ERROR(
      "Can not find data handler or data pipe on output port, output port: "
      "{0:d}--element id:{1}",
      outputPort, mId);
  return common::ErrorCode::NO_SUCH_WORKER_PORT;
}

void Element::onInputNotify() {
  ++mNotifyCount;
  mCond.notify_one();
}

void Element::addInputPort(int port) { mInputPorts.push_back(port); }
void Element::addOutputPort(int port) { mOutputPorts.push_back(port); }

std::vector<int> Element::getInputPorts() { return mInputPorts; }
std::vector<int> Element::getOutputPorts() { return mOutputPorts; };

std::size_t Element::getInputDataCount(int inputPort) const {
  auto dataPipeIt = mInputDataPipeMap.find(inputPort);
  if (mInputDataPipeMap.end() == dataPipeIt) {
    return 0;
  }

  auto inputDataPipe = dataPipeIt->second;
  if (!inputDataPipe) {
    return 0;
  }

  return inputDataPipe->getSize();
}

common::ErrorCode Element::getOutputDatapipeCapacity(int outputPort,
                                                     int& capacity) {
  auto dataPipeIt = mOutputDataPipeMap.find(outputPort);
  if (mOutputDataPipeMap.end() != dataPipeIt) {
    auto outputDataPipe = dataPipeIt->second.lock();
    if (outputDataPipe) {
      capacity = outputDataPipe->getCapacity();
      return common::ErrorCode::SUCCESS;
    }
  }
  return common::ErrorCode::NO_SUCH_WORKER_PORT;
}

common::ErrorCode Element::getOutputDatapipeSize(int outputPort, int& size) {
  auto dataPipeIt = mOutputDataPipeMap.find(outputPort);
  if (mOutputDataPipeMap.end() != dataPipeIt) {
    auto outputDataPipe = dataPipeIt->second.lock();
    if (outputDataPipe) {
      size = outputDataPipe->getSize();
      return common::ErrorCode::SUCCESS;
    }
  }
  return common::ErrorCode::NO_SUCH_WORKER_PORT;
}

}  // namespace framework
}  // namespace sophon_stream
