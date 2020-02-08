#line 2 "materialdefs_funcs_glossycoating.cl"

/***************************************************************************
 * Copyright 1998-2020 by authors (see AUTHORS.txt)                        *
 *                                                                         *
 *   This file is part of LuxCoreRender.                                   *
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

//------------------------------------------------------------------------------
// Glossycoating material
//
// One instance of this file for each Glossycoating material is used in Compiled scene
// class after having expanded the following parameters.
//
// Preprocessing parameters:
//  <<CS_GLOSSYCOATING_MATERIAL_INDEX>>
//  <<CS_MAT_BASE_MATERIAL_INDEX>>
//  <<CS_MAT_BASE_PREFIX>>_
//  <<CS_MAT_BASE_POSTFIX>>
//  <<CS_KS_TEXTURE>>
//  <<CS_NU_TEXTURE>>
//  <<CS_NV_TEXTURE>>
//  <<CS_KA_TEXTURE>>
//  <<CS_DEPTH_TEXTURE>>
//  <<CS_INDEX_TEXTURE>>
//  <<CS_MB_FLAG>>
//------------------------------------------------------------------------------

OPENCL_FORCE_NOT_INLINE float3 Material_Index<<CS_GLOSSYCOATING_MATERIAL_INDEX>>_Evaluate(__global const Material* restrict material,
		__global const HitPoint *hitPoint, const float3 lightDir, const float3 eyeDir,
		BSDFEvent *event, float *directPdfW
		MATERIALS_PARAM_DECL) {
	const float3 fixedDir = eyeDir;
	const float3 sampledDir = lightDir;

	// Note: this is the same side test used by matte translucent material and
	// it is different from the CPU test because HitPoint::dpdu and HitPoint::dpdv
	// are not available here without bump mapping.
	const float sideTest = CosTheta(lightDir) * CosTheta(eyeDir);

	if (sideTest > DEFAULT_COS_EPSILON_STATIC) {
		// Reflection

		const float3 lightDirBase = lightDir;
		const float3 eyeDirBase = eyeDir;

		const float3 baseF = <<CS_MAT_BASE_PREFIX>>_Evaluate<<CS_MAT_BASE_POSTFIX>>(&mats[<<CS_MAT_BASE_MATERIAL_INDEX>>],
				hitPoint, lightDirBase, eyeDirBase, event, directPdfW MATERIALS_PARAM);

		// Back face: no coating
		if (eyeDir.z <= 0.f)
			return baseF;

		// I have always to initialized baseF pdf because it is used below
		if (Spectrum_IsBlack(baseF)) {
			if (directPdfW)
				*directPdfW = 0.f;

			*event = NONE;
		}

		// Front face: coating+base
		*event |= GLOSSY | REFLECT;

		float3 ks = <<CS_KS_TEXTURE>>;

		const float i = <<CS_INDEX_TEXTURE>>;
		if (i > 0.f) {
			const float ti = (i - 1.f) / (i + 1.f);
			ks *= ti * ti;
		}

		ks = Spectrum_Clamp(ks);

		const float u = clamp(<<CS_NU_TEXTURE>>, 1e-9f, 1.f);
		const float v = clamp(<<CS_NV_TEXTURE>>, 1e-9f, 1.f);
		const float u2 = u * u;
		const float v2 = v * v;
		const float anisotropy = (u2 < v2) ? (1.f - u2 / v2) : u2 > 0.f ? (v2 / u2 - 1.f) : 0.f;
		const float roughness = u * v;

		if (directPdfW) {
			const float wCoating = SchlickBSDF_CoatingWeight(ks, fixedDir);
			const float wBase = 1.f - wCoating;

			*directPdfW = wBase * *directPdfW +
				wCoating * SchlickBSDF_CoatingPdf(roughness, anisotropy, fixedDir, sampledDir);
		}

		// Absorption
		const float cosi = fabs(sampledDir.z);
		const float coso = fabs(fixedDir.z);

		const float3 alpha = Spectrum_Clamp(<<CS_KA_TEXTURE>>);
		const float3 absorption = CoatingAbsorption(cosi, coso, alpha, <<CS_DEPTH_TEXTURE>>);

		// Coating fresnel factor
		const float3 H = normalize(fixedDir + sampledDir);
		const float3 S = FresnelSchlick_Evaluate(ks, fabs(dot(sampledDir, H)));

		const int multibounce = <<CS_MB_FLAG>>;
		const float3 coatingF = SchlickBSDF_CoatingF(ks, roughness, anisotropy, multibounce,
				fixedDir, sampledDir);

		// Blend in base layer Schlick style
		// assumes coating bxdf takes fresnel factor S into account

		return coatingF + absorption * (WHITE - S) * baseF;
	} else if (sideTest < -DEFAULT_COS_EPSILON_STATIC) {
		const float3 lightDirBase = lightDir;
		const float3 eyeDirBase = eyeDir;

		// Transmission
		const float3 baseF = <<CS_MAT_BASE_PREFIX>>_Evaluate<<CS_MAT_BASE_POSTFIX>>(&mats[<<CS_MAT_BASE_MATERIAL_INDEX>>],
				hitPoint, lightDirBase, eyeDirBase, event, directPdfW MATERIALS_PARAM);
		// I have always to initialized baseF pdf because it is used below
		if (Spectrum_IsBlack(baseF)) {
			if (directPdfW)
				*directPdfW = 0.f;

			*event = NONE;
		}

		*event |= GLOSSY | TRANSMIT;

		float3 ks = <<CS_KS_TEXTURE>>;

		const float i = <<CS_INDEX_TEXTURE>>;
		if (i > 0.f) {
			const float ti = (i - 1.f) / (i + 1.f);
			ks *= ti * ti;
		}

		ks = Spectrum_Clamp(ks);

		if (directPdfW) {
			const float3 fixedDir = eyeDir;
			const float wCoating = (fixedDir.z > DEFAULT_COS_EPSILON_STATIC) ? SchlickBSDF_CoatingWeight(ks, fixedDir) : 0.f;
			const float wBase = 1.f - wCoating;

			*directPdfW *= wBase;
		}

		// Absorption
		const float cosi = fabs(sampledDir.z);
		const float coso = fabs(fixedDir.z);

		const float3 alpha = Spectrum_Clamp(<<CS_KA_TEXTURE>>);
		const float3 absorption = CoatingAbsorption(cosi, coso, alpha, <<CS_DEPTH_TEXTURE>>);

		// Coating fresnel factor
		const float3 H = normalize((float3)(sampledDir.x + fixedDir.x, sampledDir.y + fixedDir.y,
			sampledDir.z - fixedDir.z));
		const float3 S = FresnelSchlick_Evaluate(ks, fabs(dot(fixedDir, H)));

		// filter base layer, the square root is just a heuristic
		// so that a sheet coated on both faces gets a filtering factor
		// of 1-S like a reflection
		return absorption * Spectrum_Sqrt(WHITE - S) * baseF;
	} else
		return BLACK;
}

OPENCL_FORCE_NOT_INLINE float3 Material_Index<<CS_GLOSSYCOATING_MATERIAL_INDEX>>_Sample(__global const Material* restrict material,
		__global const HitPoint *hitPoint, const float3 fixedDir, float3 *sampledDir, const float u0, const float u1,
		const float passThroughEvent,
		float *pdfW, BSDFEvent *event
		MATERIALS_PARAM_DECL) {
	if (fabs(fixedDir.z) < DEFAULT_COS_EPSILON_STATIC)
		return BLACK;

	float3 ks = <<CS_KS_TEXTURE>>;
	const float i = <<CS_INDEX_TEXTURE>>;
	if (i > 0.f) {
		const float ti = (i - 1.f) / (i + 1.f);
		ks *= ti * ti;
	}

	ks = Spectrum_Clamp(ks);

	// Coating is used only on the front face
	const float wCoating = (fixedDir.z > DEFAULT_COS_EPSILON_STATIC) ? SchlickBSDF_CoatingWeight(ks, fixedDir) : 0.f;
	const float wBase = 1.f - wCoating;

	const float u = clamp(<<CS_NU_TEXTURE>>, 1e-9f, 1.f);
	const float v = clamp(<<CS_NU_TEXTURE>>, 1e-9f, 1.f);
	const float u2 = u * u;
	const float v2 = v * v;
	const float anisotropy = (u2 < v2) ? (1.f - u2 / v2) : u2 > 0.f ? (v2 / u2 - 1.f) : 0.f;
	const float roughness = u * v;

	const int multibounce = <<CS_MB_FLAG>>;

	float basePdf, coatingPdf;
	float3 baseF, coatingF;
	if (passThroughEvent < wBase) {
		const float3 fixedDirBase = fixedDir;

		// Sample base BSDF
		baseF = <<CS_MAT_BASE_PREFIX>>_Sample<<CS_MAT_BASE_POSTFIX>>(&mats[<<CS_MAT_BASE_MATERIAL_INDEX>>],
			hitPoint, fixedDirBase, sampledDir, u0, u1,
				passThroughEvent / wBase,
				&basePdf, event MATERIALS_PARAM);

		if (Spectrum_IsBlack(baseF))
			return BLACK;
	
		baseF *= basePdf;

		// Don't add the coating scattering if the base sampled
		// component is specular
		if (!(*event & SPECULAR)) {
			coatingF = SchlickBSDF_CoatingF(ks, roughness, anisotropy, multibounce,
					fixedDir, *sampledDir);
			coatingPdf = SchlickBSDF_CoatingPdf(roughness, anisotropy, fixedDir, *sampledDir);
		} else {
			coatingF = BLACK;
			coatingPdf = 0.f;
		}
	} else {
		// Sample coating BSDF (Schlick BSDF)
		coatingF = SchlickBSDF_CoatingSampleF(ks, roughness, anisotropy,
				multibounce, fixedDir, sampledDir, u0, u1, &coatingPdf);
		if (Spectrum_IsBlack(coatingF))
			return BLACK;

		if (fabs(CosTheta(*sampledDir)) < DEFAULT_COS_EPSILON_STATIC)
			return BLACK;

		coatingF *= coatingPdf;

		// Evaluate base BSDF
		const float3 lightDirBase = *sampledDir;
		const float3 eyeDirBase = fixedDir;

		baseF = <<CS_MAT_BASE_PREFIX>>_Evaluate<<CS_MAT_BASE_POSTFIX>>(&mats[<<CS_MAT_BASE_MATERIAL_INDEX>>],
				hitPoint, lightDirBase, eyeDirBase, event, &basePdf MATERIALS_PARAM);
		// I have always to initialized basePdf because it is used below
		if (Spectrum_IsBlack(baseF))
			basePdf = 0.f;
		*event = GLOSSY | REFLECT;
	}

	// Absorption
	const float cosi = fabs((*sampledDir).z);
	const float coso = fabs(fixedDir.z);

	const float3 alpha = Spectrum_Clamp(<<CS_KA_TEXTURE>>);
	const float3 absorption = CoatingAbsorption(cosi, coso, alpha, <<CS_DEPTH_TEXTURE>>);

	// Note: this is the same side test used by matte translucent material and
	// it is different from the CPU test because HitPoint::dpdu and HitPoint::dpdv
	// are not available here without bump mapping.
	const float sideTest = CosTheta(fixedDir) * CosTheta(*sampledDir);
	float3 result;
	if (sideTest > DEFAULT_COS_EPSILON_STATIC) {
		// Reflection

		if (!(fixedDir.z > 0.f)) {
			// Back face reflection: no coating
			result = baseF;
		} else {
			// Front face reflection: coating+base

			// Coating fresnel factor
			const float3 H = normalize(fixedDir + *sampledDir);
			const float3 S = FresnelSchlick_Evaluate(ks, fabs(dot(*sampledDir, H)));

			// blend in base layer Schlick style
			// coatingF already takes fresnel factor S into account
			result = (coatingF + absorption * (WHITE - S) * baseF);
		}
	} else if (sideTest < -DEFAULT_COS_EPSILON_STATIC) {
		// Transmission
		// Coating fresnel factor
		float3 H = (float3)((*sampledDir).x + fixedDir.x, (*sampledDir).y + fixedDir.y,
			(*sampledDir).z - fixedDir.z);
		const float HLength = dot(H, H);
		
		float3 S;
		// I have to handle the case when HLength is 0.0 (or nearly 0.f) in
		// order to avoid NaN
		if (HLength < DEFAULT_EPSILON_STATIC)
			S = 0.f;
		else {
			// Normalize
			H *= 1.f / HLength;
			S = FresnelSchlick_Evaluate(ks, fabs(dot(fixedDir, H)));
		}

		// filter base layer, the square root is just a heuristic
		// so that a sheet coated on both faces gets a filtering factor
		// of 1-S like a reflection
		result = absorption * Spectrum_Sqrt(WHITE - S) * baseF;
	} else
		return BLACK;
	
	*pdfW = coatingPdf * wCoating + basePdf * wBase;
	result /= *pdfW;
	
	return result;
}
