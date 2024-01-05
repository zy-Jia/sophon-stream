//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// SOPHON-STREAM is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#ifndef SOPHON_STREAM_ELEMENT_PPOCR_REC_H_
#define SOPHON_STREAM_ELEMENT_PPOCR_REC_H_

#include <fstream>
#include <memory>
#include <mutex>

#include "common/profiler.h"
#include "element.h"
#include "group.h"
#include "ppocr_rec_context.h"
#include "ppocr_rec_inference.h"
#include "ppocr_rec_post_process.h"
#include "ppocr_rec_pre_process.h"

namespace sophon_stream {
namespace element {
namespace ppocr_rec {

class PpocrRec : public ::sophon_stream::framework::Element {
 public:
  PpocrRec();
  ~PpocrRec() override;

  const static std::string elementName;

  /**
   * @brief
   * 解析configure，初始化派生element的特有属性；调用initContext初始化算法相关参数
   * @param json json格式的配置文件
   * @return common::ErrorCode
   * 成功返回common::ErrorCode::SUCCESS，失败返回common::ErrorCode::PARSE_CONFIGURE_FAIL
   */
  common::ErrorCode initInternal(const std::string& json) override;

  /**
   * @brief
   * element的功能在这里实现。例如，算法模块需要实现组batch、调用算法、发送数据等功能
   * @param dataPipeId pop数据时对应的dataPipeId
   * @return common::ErrorCode 成功返回common::ErrorCode::SUCCESS
   */
  common::ErrorCode doWork(int dataPipeId) override;

  void setContext(std::shared_ptr<::sophon_stream::framework::Context> context);
  void setPreprocess(
      std::shared_ptr<::sophon_stream::framework::PreProcess> pre);
  void setInference(
      std::shared_ptr<::sophon_stream::framework::Inference> infer);
  void setPostprocess(
      std::shared_ptr<::sophon_stream::framework::PostProcess> post);
  void setStage(bool pre, bool infer, bool post);
  void initProfiler(std::string name, int interval);
  std::shared_ptr<::sophon_stream::framework::Context> getContext() {
    return mContext;
  }
  std::shared_ptr<::sophon_stream::framework::PreProcess> getPreProcess() {
    return mPreProcess;
  }
  std::shared_ptr<::sophon_stream::framework::Inference> getInference() {
    return mInference;
  }
  std::shared_ptr<::sophon_stream::framework::PostProcess> getPostProcess() {
    return mPostProcess;
  }

  /**
   * @brief 从json文件读取的配置项
   */
  static constexpr const char* CONFIG_INTERNAL_STAGE_NAME_FIELD = "stage";
  static constexpr const char* CONFIG_INTERNAL_MODEL_PATH_FIELD = "model_path";
  static constexpr const char* CONFIG_INTERNAL_BEAM_SEARCH_FIELD =
      "beam_search";
  static constexpr const char* CONFIG_INTERNAL_BEAM_WIDTH_FIELD = "beam_width";
  static constexpr const char* CONFIG_INTERNAL_CLASS_NAMES_FILE_FIELD =
      "class_names_file";

 private:
  std::shared_ptr<PpocrRecContext> mContext;          // context对象
  std::shared_ptr<PpocrRecPreProcess> mPreProcess;    // 预处理对象
  std::shared_ptr<PpocrRecInference> mInference;      // 推理对象
  std::shared_ptr<PpocrRecPostProcess> mPostProcess;  // 后处理对象

  bool use_pre = false;
  bool use_infer = false;
  bool use_post = false;

  std::string mFpsProfilerName;
  ::sophon_stream::common::FpsProfiler mFpsProfiler;

  common::ErrorCode initContext(const std::string& json);
  void process(common::ObjectMetadatas& objectMetadatas);
};

}  // namespace ppocr_rec
}  // namespace element
}  // namespace sophon_stream

#endif  // SOPHON_STREAM_ELEMENT_PPOCR_REC_H_