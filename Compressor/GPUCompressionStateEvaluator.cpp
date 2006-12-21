#include "GPUCompressionStateEvaluator.h"
#include <cassert>
#include <iostream>
#include <ctime>
#include <cmath>
#include <d3dx9.h>
#include <stack>
#include "ModelList.h"
#include "data.h"

//TODO: release surfaces

using namespace std;

#define WINDOW_WIDTH	256
#define WINDOW_HEIGHT	256

//returns the smallest number n, so that x <= 2^n
int getPo2(int x) {
	int n = 0;
	while(x > (1<<n))
		n++;
	return n; 
}

D3DPRESENT_PARAMETERS d3dparam = {
	WINDOW_WIDTH, WINDOW_HEIGHT,
	D3DFMT_A8R8G8B8,
	2, // BackBufferCount
	D3DMULTISAMPLE_NONE, 0,
	D3DSWAPEFFECT_DISCARD,
	NULL, // hDeviceWindow
	TRUE, // Windowed
	FALSE, // EnableAutoDepthStencil
	D3DFMT_D24S8, // AutoDepthStencilFormat
	0, // Flags
	0, // RefreshRateInHz
	//D3DPRESENT_INTERVAL_ONE // PresentationInterval
	D3DPRESENT_INTERVAL_IMMEDIATE
};


GPUCompressionStateEvaluator::GPUCompressionStateEvaluator() :
	m_d3d(NULL), m_device(NULL)
{
}

GPUCompressionStateEvaluator::~GPUCompressionStateEvaluator() {
	//TODO: Do lots of cleanup here :)
}

bool GPUCompressionStateEvaluator::createShaders() {
	LPD3DXBUFFER shader;
	LPD3DXBUFFER errors;

	//compile vertexshader
	HRESULT hr = D3DXCompileShader(vertexshaderSource, strlen(vertexshaderSource), NULL, NULL, "vs_main",
			D3DXGetVertexShaderProfile(m_device), 0, &shader, &errors, NULL);

	if(hr != D3D_OK) {
		MessageBox(0, (char*)errors->GetBufferPointer(), 0, 0);
		errors->Release();
		assert(false);
	}
	hr = m_device->CreateVertexShader((DWORD*)shader->GetBufferPointer(), &m_vertexShader);
	assert(hr == D3D_OK);
	shader->Release();

	//find max textures
	D3DCAPS9 caps;
	m_device->GetDeviceCaps(&caps);
	m_maxTextures = caps.MaxSimultaneousTextures;
	printf("max textures: %d\n", m_maxTextures);

	//allocate shaders
	m_sumShaders = new LPDIRECT3DPIXELSHADER9[m_maxTextures];
	m_sumLogShaders = new LPDIRECT3DPIXELSHADER9[m_maxTextures];

	//compile all shader permutations
	char enableLog[2];
	char numModels[32];
	D3DXMACRO defines[] = {
		{"DO_LOGS", enableLog},
		{"NUM_MODELS", numModels},
		{NULL, NULL}
	};
	for(int j = 0; j < 2; j++) {
		enableLog[0] = '0'+j;
		enableLog[1] = 0;
		for(int i = 0; i < m_maxTextures; i++) {
			sprintf_s(numModels, sizeof(numModels), "%d", i+1);

			hr = D3DXCompileShader(pixelshaderSource, strlen(pixelshaderSource), defines, NULL, "ps_main",
				D3DXGetPixelShaderProfile(m_device), 0, &shader, &errors, NULL);

			if(hr != D3D_OK) {
				MessageBox(0, (char*)errors->GetBufferPointer(), 0, 0);
				errors->Release();
				assert(false);
			}

			hr = m_device->CreatePixelShader((DWORD*)shader->GetBufferPointer(), j ? &m_sumLogShaders[i] : &m_sumShaders[i]);
			assert(hr == D3D_OK);
			shader->Release();
		}
	}
	return true;
}

LPD3DXMESH GPUCompressionStateEvaluator::generateQuad() {
	LPD3DXMESH mesh;
	HRESULT hr = D3DXCreateMeshFVF(2, 4, 0, D3DFVF_XYZ, m_device, &mesh);
	assert(hr == D3D_OK);

	short* iptr;
	mesh->LockIndexBuffer(0, (void**)&iptr);
	*iptr++ = 0; *iptr++ = 1; *iptr++ = 2;
	*iptr++ = 2; *iptr++ = 3; *iptr++ = 0;
	mesh->UnlockIndexBuffer();

	D3DXVECTOR3* vptr;
	mesh->LockVertexBuffer(0, (void**)&vptr);
	*vptr++ = D3DXVECTOR3(-1.0f, -1.0f, 0.0f);
	*vptr++ = D3DXVECTOR3( 1.0f, -1.0f, 0.0f);
	*vptr++ = D3DXVECTOR3( 1.0f,  1.0f, 0.0f);
	*vptr++ = D3DXVECTOR3(-1.0f,  1.0f, 0.0f);
	mesh->UnlockVertexBuffer();
	return mesh;
}

bool GPUCompressionStateEvaluator::createRenderTargets(int width, int height) {
	for(int i = 0; i < 2; i++) {
		HRESULT hr = m_device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
								D3DFMT_G32R32F, D3DPOOL_DEFAULT, &m_renderTargets[i], NULL);
		assert(hr == D3D_OK);
	}
	HRESULT hr = m_device->CreateTexture(width, height, 1, 0,
		D3DFMT_G32R32F, D3DPOOL_SYSTEMMEM, &m_systemTexture, NULL);

	//Final texture (R32F format)
	hr = m_device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_R32F, D3DPOOL_DEFAULT, &m_finalRenderTarget, NULL);
	assert(hr == D3D_OK);

	hr = m_device->CreateTexture(width, height, 1, 0,
								D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &m_finalSystemTexture, NULL);
	assert(hr == D3D_OK);

	return true;
}

LPDIRECT3DTEXTURE9 GPUCompressionStateEvaluator::generateBaseprobTexture(int width, int height, int nUsedPixels, int baseprobs[8]) {
	LPDIRECT3DTEXTURE9 texture;

	//create texture
	HRESULT hr = m_device->CreateTexture(width, height, 1, 0,
							D3DFMT_G32R32F, D3DPOOL_DEFAULT, &texture, NULL);
	assert(hr == D3D_OK);

	D3DLOCKED_RECT rect;
	hr = m_systemTexture->LockRect(0, &rect, NULL, 0);
	assert(hr == D3D_OK);
	volatile float* ptr = (float*) rect.pBits;
	//upload baseprob data
	int i = 0;
	for(; i < nUsedPixels; i++) {
		*ptr++ = baseprobs[i&7];
		*ptr++ = baseprobs[i&7];
	}

	//clear the rest
	for(; i < width*height; i++) {
		*ptr++ = 0.0f;
		*ptr++ = 0.0f;
	}
	m_systemTexture->UnlockRect(0);

	hr = m_device->UpdateTexture(m_systemTexture, texture);
	assert(hr == D3D_OK);

	return texture;
}

LPDIRECT3DTEXTURE9 GPUCompressionStateEvaluator::generateModelTexture(int width, int height, ModelPredictions& model) {
	LPDIRECT3DTEXTURE9 texture;

	//create texture
	HRESULT hr = m_device->CreateTexture(width, height, 1, 0,
		D3DFMT_G32R32F, D3DPOOL_DEFAULT, &texture, NULL);
	assert(hr == D3D_OK);

	D3DLOCKED_RECT rect;
	hr = m_systemTexture->LockRect(0, &rect, NULL, 0);
	assert(hr == D3D_OK);
	volatile float* ptr = (float*) rect.pBits;
	//upload baseprob data
	int nWeights = model.nWeights;
	Weights* weights = model.weights;
	int pos = 0;
	for(int i = 0; i < nWeights; i++) {
		int p = weights->pos & 0x7FFFFFFF;
		int boost = (weights->pos >> 30) & 0x2;
		while(pos < p) {
			*ptr++ = 0.0f;
			*ptr++ = 0.0f;
			pos++;
		}
		*ptr++ = weights->prob[0]<<boost;
		*ptr++ = weights->prob[1]<<boost;
		pos++;
		weights++;
	}

	while(pos < width*height) {
		*ptr++ = 0.0f;
		*ptr++ = 0.0f;
		pos++;
	}

	//clear the rest
	m_systemTexture->UnlockRect(0);

	hr = m_device->UpdateTexture(m_systemTexture, texture);
	assert(hr == D3D_OK);

	return texture;
}

bool GPUCompressionStateEvaluator::init(ModelPredictions* models, int length, int baseprobs[8]) {
	//create dummy window
	HWND window = CreateWindowEx(WS_EX_APPWINDOW, "EDIT",
		"dummy window",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE | WS_EX_APPWINDOW
		| WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
		NULL,
		NULL,
		NULL,
		NULL);
	assert(window);

	m_d3d = Direct3DCreate9(D3D_SDK_VERSION);
	assert(m_d3d);

	HRESULT hr;
	hr = m_d3d->CreateDevice(D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		window,
		D3DCREATE_HARDWARE_VERTEXPROCESSING,
		&d3dparam,
		&m_device);

	assert(hr == D3D_OK);

	createShaders();

	int stime = clock();

	m_length = length;
	int po2 = getPo2(length);
	m_textureHeight = 1<<(po2 / 2);
	m_textureWidth = 1<<(po2 - po2 / 2);

	printf("length: %d\n", length);

	printf("texture width: %d\n", m_textureWidth);
	printf("texture height: %d\n", m_textureHeight);

	m_quad = generateQuad();
	createRenderTargets(m_textureWidth, m_textureHeight);
	m_baseprobTexture = generateBaseprobTexture(m_textureWidth, m_textureHeight, length, baseprobs);
	for(int i = 0; i < 256; i++) {
		m_modelTextures[i] = generateModelTexture(m_textureWidth, m_textureHeight, models[i]);
	}

	m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	for(int i = 0; i < m_maxTextures; i++) {
		//m_device->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
		//m_device->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
	}
	
	int timespent = (clock() - stime)/CLOCKS_PER_SEC;
	printf("Time spent: %dm%02ds\n", timespent/60, timespent%60);

	return true;
}

struct ModelWeightPair {
	unsigned char model;
	unsigned char weight;
};

int getTime() {
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	return (int)time.QuadPart;
}

float GPUCompressionStateEvaluator::calculateSum(LPDIRECT3DTEXTURE9 texture) {
	LPDIRECT3DSURFACE9 surf;
	LPDIRECT3DSURFACE9 surf2;
	texture->GetSurfaceLevel(0, &surf);
	m_finalSystemTexture->GetSurfaceLevel(0, &surf2);
	HRESULT hr = m_device->GetRenderTargetData(surf, surf2);
	assert(hr == D3D_OK);
	D3DLOCKED_RECT rect;
	hr = surf2->LockRect(&rect, NULL, 0);
	assert(hr == D3D_OK);
	float* ptr = (float*)rect.pBits;
	float sum = 0.0f;
	for(int i = 0; i < m_length; i++) {
		sum += *ptr++;
	}
	surf2->UnlockRect();
	surf->Release();
	surf2->Release();
	return sum;
}

long long GPUCompressionStateEvaluator::evaluate(const ModelList& models) {
	int stime = getTime();

	return 0;
	//TODO: fix
/*
	stack<ModelWeightPair> mpairs;
	for(int i = 0; i < 256; i++) {
		if(models[i] != 0) {
			ModelWeightPair p = {i, 1<<(models[i]-1)};
			mpairs.push(p);
		}
	}

	int nmodels = mpairs.size();
	m_device->SetFVF(D3DFVF_XYZ);

	int destTexture = 0;
	m_device->SetVertexShader(m_vertexShader);
	D3DXVECTOR4 pixelOffset(0.5f / m_textureWidth, 0.5f / m_textureHeight, 0.0f, 0.0f);
	m_device->SetVertexShaderConstantF(0, pixelOffset, 1);

	int nBatches = nmodels;
	LPDIRECT3DTEXTURE9 startTexture = m_baseprobTexture;

	while(nBatches > 0) {
		destTexture = !destTexture;
		int nBatchsize = min(nBatches, m_maxTextures-1);
		nBatches -= nBatchsize;

		m_device->SetPixelShader(nBatches > 0 ? m_sumShaders[nBatchsize] : m_sumLogShaders[nBatchsize]);
		D3DXVECTOR4 weights[32];

		for(int i = 0; i < nBatchsize; i++) {
			ModelWeightPair p = mpairs.top(); mpairs.pop();
			weights[i].x = p.weight;
			m_device->SetTexture(i, m_modelTextures[p.model]);
		}

		LPDIRECT3DSURFACE9 surf;
		if(nBatches > 0)
			m_renderTargets[destTexture]->GetSurfaceLevel(0, &surf);
		else
			m_finalRenderTarget->GetSurfaceLevel(0, &surf);
		m_device->SetRenderTarget(0, surf);
		surf->Release();
		m_device->SetTexture(nBatchsize, startTexture);
		weights[nBatchsize].x = 1;
		m_device->SetPixelShaderConstantF(0, &weights[0].x, nBatchsize+1);

		//calculate the motherfucker!
		m_device->BeginScene();
		m_quad->DrawSubset(0);
		m_device->EndScene();
		startTexture = m_renderTargets[destTexture];
	}

	//result in m_renderTargets[destTexture]
	float compressedSize = calculateSum(m_finalRenderTarget);

	static int c = 0;
	
	if(c++ % 100 == 0) {
		cout << "nmodels: " << nmodels << "compressedSize: " << compressedSize/8.0f << endl;
	}
	return compressedSize * 4096;	//magic scale
	*/
}
