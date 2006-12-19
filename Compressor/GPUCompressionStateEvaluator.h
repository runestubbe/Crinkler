#ifndef _GPU_COMPRESSION_STATE_EVALUATOR_H
#define _GPU_COMPRESSION_STATE_EVALUATOR_H

#include <d3dx9.h>
#include "CompressionStateEvaluator.h"

class GPUCompressionStateEvaluator : public CompressionStateEvaluator {
	LPDIRECT3D9				m_d3d;
	LPDIRECT3DDEVICE9		m_device;
	LPDIRECT3DTEXTURE9		m_modelTextures[256];
	LPDIRECT3DTEXTURE9		m_baseprobTexture;
	LPDIRECT3DTEXTURE9		m_renderTargets[2];
	LPDIRECT3DTEXTURE9		m_finalRenderTarget;
	LPDIRECT3DTEXTURE9		m_systemTexture;
	LPDIRECT3DTEXTURE9		m_finalSystemTexture;
	LPDIRECT3DVERTEXSHADER9	m_vertexShader;
	LPDIRECT3DPIXELSHADER9*	m_sumShaders;
	LPDIRECT3DPIXELSHADER9*	m_sumLogShaders;
	LPD3DXMESH				m_quad;

	int						m_textureWidth;
	int						m_textureHeight;
	int						m_length;
	int						m_maxTextures;

	float calculateSum(LPDIRECT3DTEXTURE9 texture);
	
	bool createShaders();
	bool createRenderTargets(int width, int height);
	LPD3DXMESH generateQuad();
	LPDIRECT3DTEXTURE9 generateBaseprobTexture(int width, int height, int nUsedPixels, int baseprobs[8]);
	LPDIRECT3DTEXTURE9 generateModelTexture(int width, int height, ModelPredictions& model);
public:
	GPUCompressionStateEvaluator();
	~GPUCompressionStateEvaluator();

	bool init(ModelPredictions* models, int length, int baseprobs[8]);
	long long evaluate(const ModelList& models);
};

#endif