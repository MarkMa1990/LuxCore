/***************************************************************************
 * Copyright 1998-2013 by authors (see AUTHORS.txt)                        *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 * Licensed under the Apache License, Version 2.0 (the "License");         *
 * you may not use this file except in compliance with the License.        *
 * You may obtain a copy of the License at                                 *
 *                                                                         *
 *     http://www.apache.org/licenses/LICENSE-2.0                          *
 *                                                                         *
 * Unless required by applicable law or agreed to in writing, software     *
 * distributed under the License is distributed on an "AS IS" BASIS,       *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
 * See the License for the specific language governing permissions and     *
 * limitations under the License.                                          *
 ***************************************************************************/

#include <sstream>
#include <algorithm>
#include <numeric>
#include <memory>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>

#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/dassert.h>

#include "slg/imagemap/imagemap.h"
#include "slg/sdl/sdl.h"

using namespace std;
using namespace luxrays;
using namespace slg;

//------------------------------------------------------------------------------
// ImageMapStorage
//------------------------------------------------------------------------------

ImageMapStorage::StorageType ImageMapStorage::String2StorageType(const std::string &type) {
	if (type == "auto")
		return ImageMapStorage::AUTO;
	else if (type == "byte")
		return ImageMapStorage::BYTE;
	else if (type == "half")
		return ImageMapStorage::HALF;
	else if (type == "float")
		return ImageMapStorage::FLOAT;
	else
		throw runtime_error("Unknown storage type: " + type);
}

ImageMapStorage::ChannelSelectionType ImageMapStorage::String2ChannelSelectionType(
		const std::string &type) {
	if (type == "default")
		return ImageMapStorage::DEFAULT;
	else if (type == "red")
		return ImageMapStorage::RED;
	else if (type == "green")
		return ImageMapStorage::GREEN;
	else if (type == "blue")
		return ImageMapStorage::BLUE;
	else if (type == "alpha")
		return ImageMapStorage::ALPHA;
	else if (type == "mean")
		return ImageMapStorage::MEAN;
	else if (type == "colored_mean")
		return ImageMapStorage::WEIGHTED_MEAN;
	else if (type == "rgb")
		return ImageMapStorage::RGB;
	else
		throw runtime_error("Unknown channel selection type in imagemap: " + type);
}

//------------------------------------------------------------------------------
// ImageMapStorageImpl
//------------------------------------------------------------------------------

template <class T, u_int CHANNELS>
float ImageMapStorageImpl<T, CHANNELS>::GetFloat(const UV &uv) const {
	const float s = uv.u * width - .5f;
	const float t = uv.v * height - .5f;

	const int s0 = Floor2Int(s);
	const int t0 = Floor2Int(t);

	const float ds = s - s0;
	const float dt = t - t0;

	const float ids = 1.f - ds;
	const float idt = 1.f - dt;

	return ids * idt * GetTexel(s0, t0)->GetFloat() +
			ids * dt * GetTexel(s0, t0 + 1)->GetFloat() +
			ds * idt * GetTexel(s0 + 1, t0)->GetFloat() +
			ds * dt * GetTexel(s0 + 1, t0 + 1)->GetFloat();
}

template <class T, u_int CHANNELS>
float ImageMapStorageImpl<T, CHANNELS>::GetFloat(const u_int index) const {
	assert (index >= 0);
	assert (index < width * height);

	return pixels[index].GetFloat();
}

template <class T, u_int CHANNELS>
Spectrum ImageMapStorageImpl<T, CHANNELS>::GetSpectrum(const UV &uv) const {
	const float s = uv.u * width - .5f;
	const float t = uv.v * height - .5f;

	const int s0 = Floor2Int(s);
	const int t0 = Floor2Int(t);

	const float ds = s - s0;
	const float dt = t - t0;

	const float ids = 1.f - ds;
	const float idt = 1.f - dt;

	return ids * idt * GetTexel(s0, t0)->GetSpectrum() +
			ids * dt * GetTexel(s0, t0 + 1)->GetSpectrum() +
			ds * idt * GetTexel(s0 + 1, t0)->GetSpectrum() +
			ds * dt * GetTexel(s0 + 1, t0 + 1)->GetSpectrum();
}

template <class T, u_int CHANNELS>
Spectrum ImageMapStorageImpl<T, CHANNELS>::GetSpectrum(const u_int index) const {
	assert (index >= 0);
	assert (index < width * height);

	return pixels[index].GetSpectrum();
}

template <class T, u_int CHANNELS>
float ImageMapStorageImpl<T, CHANNELS>::GetAlpha(const UV &uv) const {
	const float s = uv.u * width - .5f;
	const float t = uv.v * height - .5f;

	const int s0 = Floor2Int(s);
	const int t0 = Floor2Int(t);

	const float ds = s - s0;
	const float dt = t - t0;

	const float ids = 1.f - ds;
	const float idt = 1.f - dt;

	return ids * idt * GetTexel(s0, t0)->GetAlpha() +
			ids * dt * GetTexel(s0, t0 + 1)->GetAlpha() +
			ds * idt * GetTexel(s0 + 1, t0)->GetAlpha() +
			ds * dt * GetTexel(s0 + 1, t0 + 1)->GetAlpha();
}

template <class T, u_int CHANNELS>
float ImageMapStorageImpl<T, CHANNELS>::GetAlpha(const u_int index) const {
	assert (index >= 0);
	assert (index < width * height);

	return pixels[index].GetAlpha();
}

template <class T, u_int CHANNELS>
const ImageMapPixel<T, CHANNELS> *ImageMapStorageImpl<T, CHANNELS>::GetTexel(const int s, const int t) const {
	const u_int u = Mod<int>(s, width);
	const u_int v = Mod<int>(t, height);

	const u_int index = v * width + u;
	assert (index >= 0);
	assert (index < width * height);

	return &pixels[index];
}

template <class T, u_int CHANNELS>
void ImageMapStorageImpl<T, CHANNELS>::ReverseGammaCorrection(const float gamma) {
	if (gamma != 1.f) {
		#pragma omp parallel for
		for (
				// Visusl C++ 2013 supports only OpenMP 2.5
#if _OPENMP >= 200805
				unsigned
#endif
				int i = 0; i < width * height; i++)
			pixels[i].ReverseGammaCorrection(gamma);
	}
}

template <class T, u_int CHANNELS>
ImageMapStorage *ImageMapStorageImpl<T, CHANNELS>::SelectChannel(const ChannelSelectionType selectionType) const {
	const u_int pixelCount = width * height;

	// Convert the image if required
	switch (selectionType) {
		case ImageMapStorage::DEFAULT:
			// Nothing to do
			return NULL;
		case ImageMapStorage::RED:
		case ImageMapStorage::GREEN:
		case ImageMapStorage::BLUE:
		case ImageMapStorage::ALPHA: {
			// Nothing to do
			if (CHANNELS == 1) {
				// Nothing to do
				return NULL;
			} else if (CHANNELS == 2) {
				auto_ptr<ImageMapPixel<T, 1> > newPixels(new ImageMapPixel<T, 1>[pixelCount]);

				const ImageMapPixel<T, CHANNELS> *src = pixels;
				ImageMapPixel<T, 1> *dst = newPixels.get();
				const u_int channel = (
					(selectionType == ImageMapStorage::RED) ||
					(selectionType == ImageMapStorage::GREEN) ||
					(selectionType == ImageMapStorage::BLUE)) ? 0 : 1;

				for (u_int i = 0; i < pixelCount; ++i) {
					dst->Set((T *)&src[channel]);

					src++;
					dst++;
				}

				return new ImageMapStorageImpl<T, 1>(newPixels.release(), width, height);
			} else {
				auto_ptr<ImageMapPixel<T, 1> > newPixels(new ImageMapPixel<T, 1>[pixelCount]);

				const ImageMapPixel<T, CHANNELS> *src = pixels;
				ImageMapPixel<T, 1> *dst = newPixels.get();
				const u_int channel = selectionType - ImageMapStorage::RED;

				for (u_int i = 0; i < pixelCount; ++i) {
					dst->Set((T *)&src[channel]);

					src++;
					dst++;
				}

				return new ImageMapStorageImpl<T, 1>(newPixels.release(), width, height);
			}
		}
		case ImageMapStorage::MEAN:
		case ImageMapStorage::WEIGHTED_MEAN: {
			// Nothing to do
			if (CHANNELS == 1) {
				// Nothing to do
				return NULL;
			} else if (CHANNELS == 2) {
				auto_ptr<ImageMapPixel<T, 1> > newPixels(new ImageMapPixel<T, 1>[pixelCount]);

				const ImageMapPixel<T, CHANNELS> *src = pixels;
				ImageMapPixel<T, 1> *dst = newPixels.get();
				const u_int channel = 0;

				for (u_int i = 0; i < pixelCount; ++i) {
					dst->Set((T *)&src[channel]);

					src++;
					dst++;
				}

				return new ImageMapStorageImpl<T, 1>(newPixels.release(), width, height);
			} else {
				auto_ptr<ImageMapPixel<T, 1> > newPixels(new ImageMapPixel<T, 1>[pixelCount]);

				const ImageMapPixel<T, CHANNELS> *src = pixels;
				ImageMapPixel<T, 1> *dst = newPixels.get();

				if (selectionType == ImageMapStorage::MEAN) {
					for (u_int i = 0; i < pixelCount; ++i) {
						dst->SetFloat(src->GetSpectrum().Filter());

						src++;
						dst++;
					}
				} else {
					for (u_int i = 0; i < pixelCount; ++i) {
						dst->SetFloat(src->GetSpectrum().Y());

						src++;
						dst++;
					}							
				}

				return new ImageMapStorageImpl<T, 1>(newPixels.release(), width, height);
			}
		}
		case ImageMapStorage::RGB: {
			// Nothing to do
			if ((CHANNELS == 1) || (CHANNELS == 2) || (CHANNELS == 3)) {
				// Nothing to do
				return NULL;
			} else {
				auto_ptr<ImageMapPixel<T, 3> > newPixels(new ImageMapPixel<T, 3>[pixelCount]);

				const ImageMapPixel<T, CHANNELS> *src = pixels;
				ImageMapPixel<T, 3> *dst = newPixels.get();

				for (u_int i = 0; i < pixelCount; ++i) {
					dst->Set((T *)src);

					src++;
					dst++;
				}

				return new ImageMapStorageImpl<T, 3>(newPixels.release(), width, height);
			}
		}
		default:
			throw runtime_error("Unknown channel selection type in an ImageMap: " + ToString(selectionType));
	}
}

//------------------------------------------------------------------------------
// ImageMap
//------------------------------------------------------------------------------

ImageMap::ImageMap(const string &fileName, const float g,
		const ImageMapStorage::StorageType storageType) {
	gamma = g;

	SDL_LOG("Reading texture map: " << fileName);

	if (!boost::filesystem::exists(fileName))
		throw runtime_error("ImageMap file doesn't exist: " + fileName);
	else {
		auto_ptr<ImageInput> in(ImageInput::open(fileName));

		if (in.get()) {
			const ImageSpec &spec = in->spec();

			u_int width = spec.width;
			u_int height = spec.height;
			u_int channelCount = spec.nchannels;

			if ((channelCount != 1) && (channelCount != 1) &&
					(channelCount != 3) && (channelCount != 4))
				throw runtime_error("Unsupported number of channels in an ImageMap: " + ToString(channelCount));

			ImageMapStorage::StorageType selectedStorageType = storageType;
			if (selectedStorageType == ImageMapStorage::AUTO) {
				// Automatically select the storage type

				if (spec.format == TypeDesc::UCHAR)
					selectedStorageType = ImageMapStorage::BYTE;
				else if (spec.format == TypeDesc::HALF)
					selectedStorageType = ImageMapStorage::HALF;
				else
					selectedStorageType = ImageMapStorage::FLOAT;
			}

			switch (selectedStorageType) {
				case ImageMapStorage::BYTE: {
					pixelStorage = AllocImageMapStorage<u_char>(channelCount, width, height);

					in->read_image(TypeDesc::UCHAR, pixelStorage->GetPixelsData());
					in->close();
					in.reset();
					break;
				}
				case ImageMapStorage::HALF: {
					pixelStorage = AllocImageMapStorage<half>(channelCount, width, height);

					in->read_image(TypeDesc::HALF, pixelStorage->GetPixelsData());
					in->close();
					in.reset();
					break;
				}
				case ImageMapStorage::FLOAT: {
					pixelStorage = AllocImageMapStorage<float>(channelCount, width, height);

					in->read_image(TypeDesc::FLOAT, pixelStorage->GetPixelsData());
					in->close();
					in.reset();
					break;
				}
				default:
					throw runtime_error("Unsupported selected storage type in an ImageMap: " + ToString(selectedStorageType));
			}
		} else
			throw runtime_error("Unknown image file format: " + fileName);

		pixelStorage->ReverseGammaCorrection(gamma);
	}
}

ImageMap::ImageMap(ImageMapStorage *pixels, const float g) {
	pixelStorage = pixels;
	gamma = g;
}

ImageMap::~ImageMap() {
	delete pixelStorage;
}

void ImageMap::SelectChannel(const ImageMapStorage::ChannelSelectionType selectionType) {
	ImageMapStorage *newPixelStorage = pixelStorage->SelectChannel(selectionType);

	// Replace the old image map storage if required
	if (newPixelStorage) {
		delete pixelStorage;
		pixelStorage = newPixelStorage;
	}
}

void ImageMap::Resize(const u_int newWidth, const u_int newHeight) {
	const u_int width = pixelStorage->width;
	const u_int height = pixelStorage->height;
	if ((width == newHeight) && (height == newHeight))
		return;

	ImageMapStorage::StorageType storageType = pixelStorage->GetStorageType();
	const u_int channelCount = pixelStorage->GetChannelCount();
	
	TypeDesc::BASETYPE baseType;
	switch (storageType) {
		case ImageMapStorage::BYTE:
			baseType = TypeDesc::UCHAR;
			break;
		case ImageMapStorage::HALF:
			baseType = TypeDesc::HALF;
			break;
		case ImageMapStorage::FLOAT:
			baseType = TypeDesc::FLOAT;
			break;
		default:
			throw runtime_error("Unsupported storage type in ImageMap::Resize(): " + ToString(storageType));
	}
	
	ImageSpec spec(width, height, channelCount, baseType);
	
	ImageBuf source(spec, (void *)pixelStorage->GetPixelsData());
	ImageBuf dest;
	
	ROI roi(0, newWidth, 0,newHeight, 0, 1, 0, source.nchannels());
	ImageBufAlgo::resize(dest, source, "", 0, roi);

	// I can delete the current image
	delete pixelStorage;

	// Allocate the new image map storage
	switch (storageType) {
		case ImageMapStorage::BYTE: {
			pixelStorage = AllocImageMapStorage<u_char>(channelCount, newWidth, newHeight);
			break;
		}
		case ImageMapStorage::HALF: {
			pixelStorage = AllocImageMapStorage<half>(channelCount, newWidth, newHeight);
			break;
		}
		case ImageMapStorage::FLOAT: {
			pixelStorage = AllocImageMapStorage<float>(channelCount, newWidth, newHeight);
			break;
		}
		default:
			throw runtime_error("Unsupported storage type in ImageMap::Resize(): " + ToString(storageType));
	}
	
	dest.get_pixels(0, newHeight, 0, newHeight, 0, 1, baseType, pixelStorage->GetPixelsData());
}

string ImageMap::GetFileExtension() const {
	ImageMapStorage::StorageType storageType = pixelStorage->GetStorageType();

	switch (pixelStorage->GetStorageType()) {
		case ImageMapStorage::BYTE:
			return "png";
		case ImageMapStorage::HALF:
		case ImageMapStorage::FLOAT:
			return "exr";
		default:
			throw runtime_error("Unsupported storage type in ImageMap::GetFileExtension(): " + ToString(storageType));
	}
}

void ImageMap::WriteImage(const string &fileName) const {
	ImageOutput *out = ImageOutput::create(fileName);
	if (out) {
		ImageMapStorage::StorageType storageType = pixelStorage->GetStorageType();

		switch (storageType) {
			case ImageMapStorage::BYTE: {
				ImageSpec spec(pixelStorage->width, pixelStorage->height, pixelStorage->GetChannelCount(), TypeDesc::UCHAR);
				out->open(fileName, spec);
				out->write_image(TypeDesc::UCHAR, pixelStorage->GetPixelsData());
				out->close();
				break;
			}
			case ImageMapStorage::HALF: {
				ImageSpec spec(pixelStorage->width, pixelStorage->height, pixelStorage->GetChannelCount(), TypeDesc::HALF);
				out->open(fileName, spec);
				out->write_image(TypeDesc::HALF, pixelStorage->GetPixelsData());
				out->close();
				break;
			}
			case ImageMapStorage::FLOAT: {
				ImageSpec spec(pixelStorage->width, pixelStorage->height, pixelStorage->GetChannelCount(), TypeDesc::FLOAT);
				out->open(fileName, spec);
				out->write_image(TypeDesc::FLOAT, pixelStorage->GetPixelsData());
				out->close();
				break;
			}
			default:
				throw runtime_error("Unsupported storage type in ImageMap::WriteImage(): " + ToString(storageType));
		}

		delete out;
	} else
		throw runtime_error("Failed image save: " + fileName);
}

float ImageMap::GetSpectrumMean() const {
	float mean = 0.f;	
	for (u_int y = 0; y < pixelStorage->height; ++y) {
		for (u_int x = 0; x < pixelStorage->width; ++x) {
			const u_int index = x + y * pixelStorage->width;
			
			const Spectrum s = pixelStorage->GetSpectrum(index);
			mean += (s.c[0] + s.c[1] + s.c[2]) * (1.f / 3.f);
		}
	}

	return mean / (pixelStorage->width * pixelStorage->height);
}

float ImageMap::GetSpectrumMeanY() const {
	float mean = 0.f;	
	for (u_int y = 0; y < pixelStorage->height; ++y) {
		for (u_int x = 0; x < pixelStorage->width; ++x) {
			const u_int index = x + y * pixelStorage->width;
			
			const Spectrum s = pixelStorage->GetSpectrum(index);
			mean += s.Y();
		}
	}

	return mean / (pixelStorage->width * pixelStorage->height);
}

ImageMap *ImageMap::Merge(const ImageMap *map0, const ImageMap *map1, const u_int channels,
		const u_int width, const u_int height) {
	if (channels == 1) {
		// I assume the images have the same gamma
		ImageMap *imgMap = AllocImageMap<float>(map0->GetGamma(), 1, width, height);
		float *mergedImg = (float *)imgMap->GetStorage()->GetPixelsData();

		for (u_int y = 0; y < height; ++y) {
			for (u_int x = 0; x < width; ++x) {
				const UV uv((x + .5f) / width, (y + .5f) / height);
				mergedImg[x + y * width] = map0->GetFloat(uv) * map1->GetFloat(uv);
			}
		}
		
		return imgMap;
	} else if (channels == 3) {
		// I assume the images have the same gamma
		ImageMap *imgMap = AllocImageMap<float>(map0->GetGamma(), 3, width, height);
		float *mergedImg = (float *)imgMap->GetStorage()->GetPixelsData();

		for (u_int y = 0; y < height; ++y) {
			for (u_int x = 0; x < width; ++x) {
				const UV uv((x + .5f) / width, (y + .5f) / height);
				const Spectrum c = map0->GetSpectrum(uv) * map1->GetSpectrum(uv);

				const u_int dstIndex = (x + y * width) * 3;
				mergedImg[dstIndex] = c.c[0];
				mergedImg[dstIndex + 1] = c.c[1];
				mergedImg[dstIndex + 2] = c.c[2];
			}
		}

		return imgMap;
	} else
		throw runtime_error("Unsupported number of channels in ImageMap::Merge(): " + ToString(channels));
}

ImageMap *ImageMap::Merge(const ImageMap *map0, const ImageMap *map1, const u_int channels) {
	const u_int width = Max(map0->GetWidth(), map1->GetWidth());
	const u_int height = Max(map0->GetHeight(), map1->GetHeight());

	return ImageMap::Merge(map0, map1, channels, width, height);
}

ImageMap *ImageMap::Resample(const ImageMap *map, const u_int channels,
		const u_int width, const u_int height) {
	if (channels == 1) {
		ImageMap *imgMap = AllocImageMap<float>(map->GetGamma(), 1, width, height);
		float *newImg = (float *)imgMap->GetStorage()->GetPixelsData();

		for (u_int y = 0; y < height; ++y) {
			for (u_int x = 0; x < width; ++x) {
				const UV uv((x + .5f) / width, (y + .5f) / height);
				newImg[x + y * width] = map->GetFloat(uv);
			}
		}

		return imgMap;
	} else if (channels == 3) {
		ImageMap *imgMap = AllocImageMap<float>(map->GetGamma(), 3, width, height);
		float *newImg = (float *)imgMap->GetStorage()->GetPixelsData();

		for (u_int y = 0; y < height; ++y) {
			for (u_int x = 0; x < width; ++x) {
				const UV uv((x + .5f) / width, (y + .5f) / height);
				const Spectrum c = map->GetSpectrum(uv);

				const u_int index = (x + y * width) * 3;
				newImg[index] = c.c[0];
				newImg[index + 1] = c.c[1];
				newImg[index + 2] = c.c[2];
			}
		}

		return imgMap;
	} else
		throw runtime_error("Unsupported number of channels in ImageMap::Merge(): " + ToString(channels));
}

#if defined(__APPLE__)
/**
 * Instantiate classes for the supported template type parameters
 * this is needed for dynamic linked libluxcore (further liblux linking)
 * Another solution can be moving the function definitions to header file
 * http://stackoverflow.com/questions/1022623/c-shared-library-with-templates-undefined-symbols-error
 */
template class ImageMapStorageImpl<float, 1u>;
template class ImageMapStorageImpl<float, 2u>;
template class ImageMapStorageImpl<float, 3u>;
template class ImageMapStorageImpl<float, 4u>;
template class ImageMapStorageImpl<unsigned char, 1u>;
template class ImageMapStorageImpl<unsigned char, 2u>;
template class ImageMapStorageImpl<unsigned char, 3u>;
template class ImageMapStorageImpl<unsigned char, 4u>;
#endif
