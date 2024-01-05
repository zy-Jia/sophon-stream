//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// SOPHON-STREAM is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "ppocr_det_pre_process.h"

#include "common/logger.h"

namespace sophon_stream {
namespace element {
namespace ppocr_det {

double pythonRound(double number) {
  double integer_part = 0.0;
  double fractional_part = std::modf(number, &integer_part);

  if (fractional_part > 0.5 ||
      (fractional_part == 0.5 && fmod(integer_part, 2.0) == 1.0)) {
    integer_part += 1.0;
  }

  return integer_part;
}

void Ppocr_detPreProcess::init(std::shared_ptr<Ppocr_detContext> context) {}

void Ppocr_detPreProcess::initTensors(
    std::shared_ptr<Ppocr_detContext> context,
    common::ObjectMetadatas& objectMetadatas) {
  for (auto& obj : objectMetadatas) {
    obj->mInputBMtensors = std::make_shared<sophon_stream::common::bmTensors>();
    int channelId = obj->mFrame->mChannelId;
    int frameId = obj->mFrame->mFrameId;
    obj->mInputBMtensors.reset(
        new sophon_stream::common::bmTensors(),
        [channelId, frameId](sophon_stream::common::bmTensors* p) {
          for (int i = 0; i < p->tensors.size(); ++i) {
            if (p->tensors[i]->device_mem.u.device.device_addr != 0) {
              bm_free_device(p->handle, p->tensors[i]->device_mem);
            }
          }

          delete p;
          p = nullptr;
        });
    obj->mInputBMtensors->handle = context->handle;
    obj->mInputBMtensors->tensors.resize(context->input_num);
    for (int i = 0; i < context->input_num; ++i) {
      obj->mInputBMtensors->tensors[i] = std::make_shared<bm_tensor_t>();
      obj->mInputBMtensors->tensors[i]->dtype =
          context->bmNetwork->m_netinfo->input_dtypes[i];
      obj->mInputBMtensors->tensors[i]->shape =
          context->bmNetwork->m_netinfo->stages[0].input_shapes[i];
      obj->mInputBMtensors->tensors[i]->shape.dims[0] = 1;
      obj->mInputBMtensors->tensors[i]->st_mode = BM_STORE_1N;
    }
  }
}

common::ErrorCode Ppocr_detPreProcess::preProcess(
    std::shared_ptr<Ppocr_detContext> context,
    common::ObjectMetadatas& objectMetadatas) {
  if (objectMetadatas.size() == 0) return common::ErrorCode::SUCCESS;
  initTensors(context, objectMetadatas);

  auto jsonPlanner = context->bgr2rgb ? FORMAT_RGB_PLANAR : FORMAT_BGR_PLANAR;
  int i = 0;
  for (auto& objMetadata : objectMetadatas) {
    if (objMetadata->mFrame->mSpData == nullptr) continue;
    bm_image resized_img;
    bm_image converto_img;
    bm_image image0 = *objMetadata->mFrame->mSpData;
    bm_image image1;
    // convert to RGB_PLANAR
    if (image0.image_format != jsonPlanner) {
      bm_image_create(context->handle, image0.height, image0.width, jsonPlanner,
                      image0.data_type, &image1);
      bm_image_alloc_dev_mem_heap_mask(image1, 2);
      bmcv_image_storage_convert(context->handle, 1, &image0, &image1);
    } else {
      image1 = image0;
    }
    bm_image image_aligned;
    bool need_copy = image1.width & (64 - 1);
    if (need_copy) {
      int stride1[3], stride2[3];
      bm_image_get_stride(image1, stride1);
      stride2[0] = FFALIGN(stride1[0], 64);
      stride2[1] = FFALIGN(stride1[1], 64);
      stride2[2] = FFALIGN(stride1[2], 64);
      bm_image_create(context->bmContext->handle(), image1.height, image1.width,
                      image1.image_format, image1.data_type, &image_aligned,
                      stride2);

      bm_image_alloc_dev_mem_heap_mask(image_aligned, 2);
      bmcv_copy_to_atrr_t copyToAttr;
      memset(&copyToAttr, 0, sizeof(copyToAttr));
      copyToAttr.start_x = 0;
      copyToAttr.start_y = 0;
      copyToAttr.if_padding = 1;
      bmcv_image_copy_to(context->bmContext->handle(), copyToAttr, image1,
                         image_aligned);
    } else {
      image_aligned = image1;
    }

    int w = image_aligned.width;
    int h = image_aligned.height;

    float ratio = 1.;
    int max_wh = w >= h ? w : h;
    if (max_wh > context->net_w) {
      ratio = float(context->net_w) / max_wh;
    } else {
      ratio = 1;
    }
    int resize_h = int(h * ratio);
    int resize_w = int(w * ratio);

    resize_h = std::max(int(pythonRound((float)resize_h / 32) * 32), 32);
    resize_w = std::max(int(pythonRound((float)resize_w / 32) * 32), 32);

    objMetadata->resize_vector.push_back(resize_h);
    objMetadata->resize_vector.push_back(resize_w);

    bmcv_padding_atrr_t padding_attr;
    memset(&padding_attr, 0, sizeof(padding_attr));
    padding_attr.dst_crop_sty = 0;
    padding_attr.dst_crop_stx = 0;
    padding_attr.dst_crop_w = resize_w;
    padding_attr.dst_crop_h = resize_h;
    padding_attr.padding_b = 0;
    padding_attr.padding_g = 0;
    padding_attr.padding_r = 0;
    padding_attr.if_memset = 1;

    int aligned_net_w = FFALIGN(context->net_w, 64);
    int strides[3] = {aligned_net_w, aligned_net_w, aligned_net_w};
    bm_image_create(context->handle, context->net_h, context->net_w,
                    jsonPlanner, DATA_TYPE_EXT_1N_BYTE, &resized_img, strides);
#if BMCV_VERSION_MAJOR > 1
    bm_image_alloc_dev_mem_heap_mask(resized_img, 2);
#else
    bm_image_alloc_dev_mem_heap_mask(resized_img, 4);
#endif

    bmcv_rect_t crop_rect{0, 0, image1.width, image1.height};
    bm_status_t ret = BM_SUCCESS;
    ret = bmcv_image_vpp_convert_padding(context->bmContext->handle(), 1,
                                         image_aligned, &resized_img,
                                         &padding_attr, &crop_rect);
    assert(BM_SUCCESS == ret);

    if (image0.image_format != FORMAT_BGR_PLANAR) {
      bm_image_destroy(image1);
    }
    if (need_copy) bm_image_destroy(image_aligned);

    bm_image_data_format_ext img_dtype = DATA_TYPE_EXT_FLOAT32;
    auto tensor = context->bmNetwork->inputTensor(0);
    if (tensor->get_dtype() == BM_INT8) {
      img_dtype = DATA_TYPE_EXT_1N_BYTE_SIGNED;
    }

    bm_image_create(context->handle, context->net_h, context->net_w,
                    jsonPlanner, img_dtype, &converto_img);

    bm_device_mem_t mem;
    int size_byte = 0;
    bm_image_get_byte_size(converto_img, &size_byte);
    ret = bm_malloc_device_byte_heap(context->handle, &mem, 0, size_byte);
    bm_image_attach(converto_img, &mem);

    bmcv_image_convert_to(context->handle, 1, context->converto_attr,
                          &resized_img, &converto_img);

    bm_image_destroy(resized_img);

    bm_image_get_device_mem(
        converto_img,
        &objectMetadatas[i]->mInputBMtensors->tensors[0]->device_mem);

    bm_image_detach(converto_img);
    bm_image_destroy(converto_img);
    i++;
  }
  return common::ErrorCode::SUCCESS;
}

}  // namespace ppocr_det
}  // namespace element
}  // namespace sophon_stream