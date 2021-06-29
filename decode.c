/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "video.h"
#include "ffmpeg.h"

#include "sdl-state.h"


#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_syswm.h>
#include <d3d11.h>
#define SDL_BUFFER_FRAMES 2

#define DECODER_BUFFER_SIZE 1024 * 1024

static unsigned char* ffmpeg_buffer;

FILE* file;
/*ID3D11Device* device;
ID3D11DeviceContext* deviceContext;*/
AVFrame* destFrame;
static int sdl_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags) {
  int avc_flags = LOW_LATENCY_DECODE | SLICE_THREADING;

  if (ffmpeg_init(videoFormat, width, height, avc_flags, SDL_BUFFER_FRAMES,4) < 0) {
    fprintf(stderr, "Couldn't initialize video decoding\n");
    return -1;
  }

  ffmpeg_buffer = (unsigned char *) malloc(DECODER_BUFFER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
  if (ffmpeg_buffer == NULL) {
    fprintf(stderr, "Not enough memory\n");
    ffmpeg_destroy();
    return -1;
  }

  char* pFile[2048];
  sprintf(pFile, "%s%s", SDL_GetPrefPath("Moonlight","Xbox"), "performance.log");
  file = fopen(pFile, "w");
  /*D3D_FEATURE_LEVEL fl[1] = {
      D3D_FEATURE_LEVEL_11_0
  };
  HRESULT r = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, fl, 1, D3D11_SDK_VERSION, &device,NULL,&deviceContext);*/
  destFrame = av_frame_alloc();
  return 0;
}

static void sdl_cleanup() {
  ffmpeg_destroy();
}

static int sdl_submit_decode_unit(PDECODE_UNIT decodeUnit) {
  if (decodeUnit->fullLength < DECODER_BUFFER_SIZE) {
    PLENTRY entry = decodeUnit->bufferList;
    int length = 0;
    
    while (entry != NULL) {
      memcpy(ffmpeg_buffer+length, entry->data, entry->length);
      length += entry->length;
      entry = entry->next;
    }
    int t = SDL_GetTicks();
    int status = ffmpeg_decode(ffmpeg_buffer, length, decodeUnit->frameType == FRAME_TYPE_IDR);
    if (status == DR_NEED_IDR)return DR_NEED_IDR;
    int t2 = SDL_GetTicks();
    fprintf(file, "Got %d ticks for decoding frame %d of type %d\r\n", t2 - t, decodeUnit->frameNumber,decodeUnit->frameType);
    if (SDL_LockMutex(mutex) == 0) {
      AVFrame* frame = ffmpeg_get_frame(false);
      if (frame != NULL) {
        sdlNextFrame++;
        /*ID3D11Texture2D *d3dTexture = NULL;
        d3dTexture = frame->data[0];
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        D3D11_TEXTURE2D_DESC desc,destDesc;
        memset(&destDesc, 0, sizeof(D3D11_TEXTURE2D_DESC));
        d3dTexture->lpVtbl->GetDesc(d3dTexture, &desc);
        ID3D11Texture2D* copyTexture;
        destDesc.Usage = D3D11_USAGE_STAGING;
        destDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        destDesc.Width = desc.Width;
        destDesc.Height = desc.Height;
        destDesc.SampleDesc = desc.SampleDesc;
        destDesc.Format = DXGI_FORMAT_NV12;
        destDesc.MipLevels = 1;
        destDesc.ArraySize = 1;
        destDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;*/
        //AVFrame *destFrame = av_frame_alloc();
        
        destFrame->format = AV_PIX_FMT_NV12;
        int r = av_hwframe_transfer_data(destFrame, frame, 0);
        //HRESULT res = device->lpVtbl->CreateTexture2D(device, &destDesc, NULL, &copyTexture);
        //deviceContext->lpVtbl->CopyResource(deviceContext, &copyTexture, &d3dTexture);
        //deviceContext->lpVtbl->CopySubresourceRegion(deviceContext,copyTexture, 0, 0, 0, 0, d3dTexture, index, NULL);
       // res = deviceContext->lpVtbl->Map(deviceContext, copyTexture,0, D3D11_MAP_READ, 0, &mappedResource);
        SDL_Event event;
        event.type = SDL_USEREVENT;
        event.user.code = SDL_CODE_FRAME;
        event.user.data1 = &destFrame->data;
        event.user.data2 = &destFrame->linesize;
        SDL_PushEvent(&event);
      }

      SDL_UnlockMutex(mutex);
    } else
      set_text(stderr, "Couldn't lock mutex\n");
  } else {
      char errorText[2048];
      sprintf("Buffer to small, got %d", decodeUnit->fullLength);
      set_text(errorText);
    //fprintf(stderr, "Video decode buffer too small");
    //exit(1);
  }

  return DR_OK;
}

static void sdl_start() {

}

static void sdl_stop() {

}



DECODER_RENDERER_CALLBACKS get_video_callback() {
    DECODER_RENDERER_CALLBACKS decoder_callbacks_sdl;
    decoder_callbacks_sdl.setup = sdl_setup;
    decoder_callbacks_sdl.start = sdl_start;
    decoder_callbacks_sdl.stop = sdl_stop;
    decoder_callbacks_sdl.cleanup = sdl_cleanup;
    decoder_callbacks_sdl.submitDecodeUnit = sdl_submit_decode_unit;
    decoder_callbacks_sdl.capabilities = CAPABILITY_SLICES_PER_FRAME(4) | CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC | CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC | CAPABILITY_DIRECT_SUBMIT;
    return decoder_callbacks_sdl;
}
