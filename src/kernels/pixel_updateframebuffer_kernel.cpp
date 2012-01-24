#include "luxrays/kernels/kernels.h" 
std::string luxrays::KernelSource_Pixel_UpdateFrameBuffer = 
"/***************************************************************************\n" 
"*   Copyright (C) 1998-2010 by authors (see AUTHORS.txt )                 *\n" 
"*                                                                         *\n" 
"*   This file is part of LuxRays.                                         *\n" 
"*                                                                         *\n" 
"*   LuxRays is free software; you can redistribute it and/or modify       *\n" 
"*   it under the terms of the GNU General Public License as published by  *\n" 
"*   the Free Software Foundation; either version 3 of the License, or     *\n" 
"*   (at your option) any later version.                                   *\n" 
"*                                                                         *\n" 
"*   LuxRays is distributed in the hope that it will be useful,            *\n" 
"*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *\n" 
"*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *\n" 
"*   GNU General Public License for more details.                          *\n" 
"*                                                                         *\n" 
"*   You should have received a copy of the GNU General Public License     *\n" 
"*   along with this program.  If not, see <http://www.gnu.org/licenses/>. *\n" 
"*                                                                         *\n" 
"*   LuxRays website: http://www.luxrender.net                             *\n" 
"***************************************************************************/\n" 
"typedef struct {\n" 
"float r, g, b;\n" 
"} Spectrum;\n" 
"typedef struct {\n" 
"Spectrum radiance;\n" 
"float weight;\n" 
"} SamplePixel;\n" 
"typedef Spectrum Pixel;\n" 
"#define GAMMA_TABLE_SIZE 1024u\n" 
"float Clamp(float val, float low, float high) {\n" 
"return (val > low) ? ((val < high) ? val : high) : low;\n" 
"}\n" 
"unsigned int Floor2UInt(const float val) {\n" 
"return (val > 0.f) ? ((unsigned int)floor(val)) : 0;\n" 
"}\n" 
"float Radiance2PixelFloat(\n" 
"const float x,\n" 
"__constant float *gammaTable) {\n" 
"//return powf(Clamp(x, 0.f, 1.f), 1.f / 2.2f);\n" 
"const unsigned int index = min(\n" 
"Floor2UInt(GAMMA_TABLE_SIZE * Clamp(x, 0.f, 1.f)),\n" 
"GAMMA_TABLE_SIZE - 1u);\n" 
"return gammaTable[index];\n" 
"}\n" 
"__kernel __attribute__((reqd_work_group_size(8, 8, 1))) void PixelUpdateFrameBuffer(\n" 
"const unsigned int width,\n" 
"const unsigned int height,\n" 
"__global SamplePixel *sampleFrameBuffer,\n" 
"__global Pixel *frameBuffer,\n" 
"__constant __attribute__((max_constant_size(sizeof(float) * GAMMA_TABLE_SIZE))) float *gammaTable) {\n" 
"const unsigned int px = get_global_id(0);\n" 
"if(px >= width)\n" 
"return;\n" 
"const unsigned int py = get_global_id(1);\n" 
"if(py >= height)\n" 
"return;\n" 
"const unsigned int offset = px + py * width;\n" 
"__global SamplePixel *sp = &sampleFrameBuffer[offset];\n" 
"__global Pixel *p = &frameBuffer[offset];\n" 
"const float weight = sp->weight;\n" 
"if (weight == 0.f)\n" 
"return;\n" 
"const float invWeight = 1.f / weight;\n" 
"p->r = Radiance2PixelFloat(sp->radiance.r * invWeight, gammaTable);\n" 
"p->g = Radiance2PixelFloat(sp->radiance.g * invWeight, gammaTable);\n" 
"p->b = Radiance2PixelFloat(sp->radiance.b * invWeight, gammaTable);\n" 
"}\n" 
; 
