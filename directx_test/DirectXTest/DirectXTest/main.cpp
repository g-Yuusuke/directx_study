#include <Windows.h>
#include <iostream>
#include <debugapi.h>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include<d3dcompiler.h>

#include <DirectXMath.h>
#include <DirectXTex.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#pragma comment(lib, "DirectXTex.lib")

using namespace std;
using namespace DirectX;

constexpr unsigned int WINDOW_WIDTH = 1280;
constexpr unsigned int WINDOW_HEIGHT = 720;

ID3D12Device* device = nullptr;
IDXGIFactory6* dxgiFactory = nullptr;
IDXGISwapChain4* swapChain = nullptr;
ID3D12CommandAllocator* commandAllocator = nullptr;
ID3D12GraphicsCommandList* commandList = nullptr;
ID3D12CommandQueue* commandQueue = nullptr;
ID3D12DescriptorHeap* rtvHeaps = nullptr;
ID3D12DescriptorHeap* basicHeaps = nullptr;

ID3D12Fence* fence = nullptr;
UINT64 fenceValue = 0;

ID3D12Resource* vertexBuffer = nullptr;
ID3D12Resource* indexBuffer = nullptr;
ID3D12Resource* uploadBuffer = nullptr;
ID3D12Resource* textureBuffer = nullptr;
ID3D12Resource* constantBuffer = nullptr;

struct Vertex
{
	XMFLOAT3 pos;
	XMFLOAT2 uv;
};
Vertex vertices[] = {
	{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
	{ { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
	{ { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
	{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
};
unsigned short indices[] = {
	0, 1, 2,
	2, 1, 3,
};

float angle = XM_PIDIV4;

ID3D12RootSignature* rootSignature = nullptr;
ID3D12PipelineState* pipelineState = nullptr;

TexMetadata textureMetaData = {};
ScratchImage textureData = {};

struct PMDHeader
{
	float version;
	char model_name[20];
	char comment[256];
};

struct PMDVertex
{
	XMFLOAT3 pos;
	XMFLOAT3 normal;
	XMFLOAT2 uv;
	unsigned short bone_no[2];
	unsigned char bone_weight;
	unsigned char edge_flag;
};
constexpr size_t PMD_VERTEX_SIZE = 38;

PMDHeader modelHeader = {};
vector<PMDVertex> modelVertices;
vector<unsigned short> modelIndices;

// テクスチャ読み込み
void GenerateTextureData()
{
	HRESULT result = S_OK;
	result = CoInitializeEx(0, COINIT_MULTITHREADED);
	result = LoadFromWICFile(L"Resource/Texture/textest.png", WIC_FLAGS_NONE, &textureMetaData, textureData);
}

// モデル読み込み
void GenerateModelData()
{
	char signature[3] = {};
	FILE* fp = nullptr;
	errno_t error = fopen_s(&fp, "Resource/Model/初音ミク.pmd", "rb");

	fread(signature, sizeof(signature), 1, fp);
	fread(&modelHeader, sizeof(modelHeader), 1, fp);

	unsigned int vertexNum;
	fread(&vertexNum, sizeof(vertexNum), 1, fp);

	modelVertices.resize(vertexNum);
	fread(modelVertices.data(), PMD_VERTEX_SIZE * modelVertices.size(), 1, fp);

	unsigned int indexNum;
	fread(&indexNum, sizeof(indexNum), 1, fp);

	modelIndices.resize(indexNum);
	fread(modelIndices.data(), sizeof(modelIndices[0]) * modelIndices.size(), 1, fp);
	
	fclose(fp);
}

// DirectX 初期化
void DxInitialize(HWND hwnd)
{
	// デバッグレイヤーを有効化
	ID3D12Debug* debugLayer = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer)))) {
		debugLayer->EnableDebugLayer();
		debugLayer->Release();
	}

	if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgiFactory)))) {
		CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	}

	// アダプターを列挙
	vector<IDXGIAdapter*> adapters;
	IDXGIAdapter* tempAdapter = nullptr;
	for (int i = 0; dxgiFactory->EnumAdapters(i, &tempAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
		adapters.push_back(tempAdapter);
	}

	// 使用するアダプターを決定
	tempAdapter = nullptr;
	for (auto adapter : adapters) {
		DXGI_ADAPTER_DESC adapterDesc = {};
		adapter->GetDesc(&adapterDesc);

		// 開発しているPCのグラフィックボードがDirectX 12に対応していなかったため、
		// Microsoft Basic Display Driverを使用
		wstring str = adapterDesc.Description;
		if (str.find(L"Microsoft") != string::npos) {
			tempAdapter = adapter;
			break;
		}
	}

	D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	// 使用可能な最も高いフィーチャーレベルでデバイスを作成
	for (auto lv : levels) {
		auto result = D3D12CreateDevice(tempAdapter, lv, IID_PPV_ARGS(&device));
		if (result == S_OK) {
			break;
		}
	}

	HRESULT result = S_OK;
	// コマンドアロケーターを作成
	result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	// コマンドアロケーターに紐付くコマンドリストを作成
	result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));

	// コマンドキューを作成
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
	commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	commandQueueDesc.NodeMask = 0;
	commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	result = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

	// ウィンドウに対してスワップチェーンを作成
	// バックバッファーは2つ分用意
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = WINDOW_WIDTH;
	swapChainDesc.Height = WINDOW_HEIGHT;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = false;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	result = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, (IDXGISwapChain1**)&swapChain);

	// レンダーターゲットビューを作成
	// まずディスクリプタヒープを準備
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	result = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));

	// レンダーターゲットビューの設定にsRGBを指定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// スワップチェーンと紐付けてレンダーターゲットビューを作成
	DXGI_SWAP_CHAIN_DESC tempDesc = {};
	result = swapChain->GetDesc(&tempDesc);
	vector<ID3D12Resource*> backBuffers(tempDesc.BufferCount);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
	UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (int i = 0; i < tempDesc.BufferCount; ++i) {
		result = swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
		device->CreateRenderTargetView(backBuffers[i], &rtvDesc, handle);

		handle.ptr += stride;
	}

	// フェンスの準備
	result = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

	GenerateModelData();

	// 頂点バッファー作成
	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 0;
	heapProp.VisibleNodeMask = 0;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = PMD_VERTEX_SIZE * modelVertices.size();
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	result = device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer));

	// マップして頂点情報を書き込み
	PMDVertex* vertexMap = nullptr;
	result = vertexBuffer->Map(0, nullptr, (void**)&vertexMap);
	copy(begin(modelVertices), end(modelVertices), vertexMap);
	vertexBuffer->Unmap(0, nullptr);

	// 同様にインデックスバッファーを作成
	resourceDesc.Width = sizeof(modelIndices[0]) * modelIndices.size();

	result = device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer));

	// マップしてインデックス情報を書き込み
	unsigned short* indexMap = nullptr;
	result = indexBuffer->Map(0, nullptr, (void**)&indexMap);
	copy(begin(modelIndices), end(modelIndices), indexMap);
	indexBuffer->Unmap(0, nullptr);

	// テクスチャ読み込み
	GenerateTextureData();
	auto image = textureData.GetImage(0, 0, 0);

	// テクスチャアップロード用のバッファーを作成
	// CPU側からはこのバッファーにテクスチャを書き込む
	auto alignmentedSize = (image->rowPitch + 0xFF) & ~0xFF;
	resourceDesc.Width = alignmentedSize * image->height;

	result = device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));

	// GPU側で利用するテクスチャバッファーを作成
	heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 0;
	heapProp.VisibleNodeMask = 0;

	resourceDesc = {};
	resourceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(textureMetaData.dimension);
	resourceDesc.Width = textureMetaData.width;
	resourceDesc.Height = textureMetaData.height;
	resourceDesc.DepthOrArraySize = textureMetaData.arraySize;
	resourceDesc.MipLevels = textureMetaData.mipLevels;
	resourceDesc.Format = textureMetaData.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	result = device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textureBuffer));

	// アップロード用バッファーにテクスチャを書き込み
	uint8_t* imageMap = nullptr;
	result = uploadBuffer->Map(0, nullptr, (void**)&imageMap);
	for (int y = 0; y < image->height; ++y) {
		copy_n(image->pixels + y * image->rowPitch, alignmentedSize, imageMap + y * alignmentedSize);
	}
	uploadBuffer->Unmap(0, nullptr);

	// 定数バッファーを作成
	// 座標変換用の行列を転送するために使用
	heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 0;
	heapProp.VisibleNodeMask = 0;

	resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = (sizeof(XMMATRIX) + 0xFF) & ~0xFF;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	result = device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer));

	// シェーダーリソースビューと定数バッファービューのためのディスクリプタヒープを作成
	heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	result = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&basicHeaps));

	// シェーダーリソースビューはディスクリプタヒープ上の先頭に配置
	handle = basicHeaps->GetCPUDescriptorHandleForHeapStart();

	// ディスクリプタヒープ上にシェーダーリソースビューを作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = textureMetaData.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(textureBuffer, &srvDesc, handle);

	// 定数バッファービューはその次に配置するため位置を進める
	handle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// ディスクリプタヒープ上に定数バッファービューを作成
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = constantBuffer->GetDesc().Width;
	device->CreateConstantBufferView(&cbvDesc, handle);

	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;

	ID3DBlob* errorBlob = nullptr;

	// 頂点シェーダーをコンパイル
	result = D3DCompileFromFile(
		L"BasicVertexShader.hlsl",
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"BasicVS",
		"vs_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&vsBlob,
		&errorBlob
	);
	if (FAILED(result)) {
		std::string message;
		message.resize(errorBlob->GetBufferSize());
		copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), message.begin());
		message += "\n";
		OutputDebugStringA(message.c_str());
	}

	// ピクセルシェーダーをコンパイル
	result = D3DCompileFromFile(
		L"BasicPixelShader.hlsl",
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"BasicPS",
		"ps_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&psBlob,
		&errorBlob
	);
	if (FAILED(result)) {
		std::string message;
		message.resize(errorBlob->GetBufferSize());
		copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), message.begin());
		message += "\n";
		OutputDebugStringA(message.c_str());
	}

	// 頂点レイアウトの設定
	// 頂点ごとにPMDに合わせた情報を渡す
	D3D12_INPUT_ELEMENT_DESC posLayout = {};
	posLayout.SemanticName = "POSITION";
	posLayout.SemanticIndex = 0;
	posLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	posLayout.InputSlot = 0;
	posLayout.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	posLayout.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	posLayout.InstanceDataStepRate = 0;

	D3D12_INPUT_ELEMENT_DESC normalLayout = {};
	normalLayout.SemanticName = "NORMAL";
	normalLayout.SemanticIndex = 0;
	normalLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	normalLayout.InputSlot = 0;
	normalLayout.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	normalLayout.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	normalLayout.InstanceDataStepRate = 0;

	D3D12_INPUT_ELEMENT_DESC uvLayout = {};
	uvLayout.SemanticName = "TEXCOORD";
	uvLayout.SemanticIndex = 0;
	uvLayout.Format = DXGI_FORMAT_R32G32_FLOAT;
	uvLayout.InputSlot = 0;
	uvLayout.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	uvLayout.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	uvLayout.InstanceDataStepRate = 0;

	D3D12_INPUT_ELEMENT_DESC boneNoLayout = {};
	boneNoLayout.SemanticName = "BONE_NO";
	boneNoLayout.SemanticIndex = 0;
	boneNoLayout.Format = DXGI_FORMAT_R16G16B16A16_UINT;
	boneNoLayout.InputSlot = 0;
	boneNoLayout.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	boneNoLayout.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	boneNoLayout.InstanceDataStepRate = 0;

	D3D12_INPUT_ELEMENT_DESC weightLayout = {};
	weightLayout.SemanticName = "WEIGHT";
	weightLayout.SemanticIndex = 0;
	weightLayout.Format = DXGI_FORMAT_R8_UINT;
	weightLayout.InputSlot = 0;
	weightLayout.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	weightLayout.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	weightLayout.InstanceDataStepRate = 0;

	D3D12_INPUT_ELEMENT_DESC edgeFlagLayout = {};
	edgeFlagLayout.SemanticName = "EDGE_FLAG";
	edgeFlagLayout.SemanticIndex = 0;
	edgeFlagLayout.Format = DXGI_FORMAT_R8_UINT;
	edgeFlagLayout.InputSlot = 0;
	edgeFlagLayout.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	edgeFlagLayout.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	edgeFlagLayout.InstanceDataStepRate = 0;

	D3D12_INPUT_ELEMENT_DESC inputLayouts[] = {
		posLayout,
		normalLayout,
		uvLayout,
		boneNoLayout,
		weightLayout,
		edgeFlagLayout,
	};

	// グラフィックパイプラインステートの作成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};

	// ディスクリプタレンジの指定
	// シェーダーリソースビューと定数バッファービューを使用
	D3D12_DESCRIPTOR_RANGE descRange[2] = {};
	descRange[0].NumDescriptors = 1;
	descRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descRange[0].BaseShaderRegister = 0;
	descRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	descRange[1].NumDescriptors = 1;
	descRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descRange[1].BaseShaderRegister = 0;
	descRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ルートパラメーターの設定
	// ディスクリプタテーブルとして使用
	D3D12_ROOT_PARAMETER rootParam = {};
	rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParam.DescriptorTable.pDescriptorRanges = descRange;
	rootParam.DescriptorTable.NumDescriptorRanges = 2;

	// サンプラーの設定
	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

	// ルートシグネチャの設定
	// ルートパラメーターとサンプラーを指定する
	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSigDesc.pParameters = &rootParam;
	rootSigDesc.NumParameters = 1;
	rootSigDesc.pStaticSamplers = &samplerDesc;
	rootSigDesc.NumStaticSamplers = 1;
	
	// ルートシグネチャを作成
	ID3D10Blob* rootSigBlob = nullptr;
	result = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSigBlob, &errorBlob);
	if (FAILED(result)) {
		std::string message;
		message.resize(errorBlob->GetBufferSize());
		copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), message.begin());
		message += "\n";
		OutputDebugStringA(message.c_str());
	}
	result = device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	rootSigBlob->Release();
	// グラフィックスパイプラインステートにルートシグネチャを設定
	pipelineStateDesc.pRootSignature = rootSignature;

	// シェーダーの設定
	pipelineStateDesc.VS.pShaderBytecode = vsBlob->GetBufferPointer();
	pipelineStateDesc.VS.BytecodeLength = vsBlob->GetBufferSize();
	pipelineStateDesc.PS.pShaderBytecode = psBlob->GetBufferPointer();
	pipelineStateDesc.PS.BytecodeLength = psBlob->GetBufferSize();

	// サンプルマスクとラスタライザーステートの設定
	pipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	pipelineStateDesc.RasterizerState.MultisampleEnable = false;
	pipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pipelineStateDesc.RasterizerState.DepthClipEnable = true;
	pipelineStateDesc.RasterizerState.FrontCounterClockwise = false;
	pipelineStateDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	pipelineStateDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	pipelineStateDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	pipelineStateDesc.RasterizerState.AntialiasedLineEnable = false;
	pipelineStateDesc.RasterizerState.ForcedSampleCount = 0;
	pipelineStateDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	// ブレンドステートの設定
	pipelineStateDesc.BlendState.AlphaToCoverageEnable = false;
	pipelineStateDesc.BlendState.IndependentBlendEnable = false;

	D3D12_RENDER_TARGET_BLEND_DESC blendDesc = {};
	blendDesc.BlendEnable = false;
	blendDesc.LogicOpEnable = false;
	blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pipelineStateDesc.BlendState.RenderTarget[0] = blendDesc;

	// 深度バッファー、ステンシルバッファーの設定
	pipelineStateDesc.DepthStencilState.DepthEnable = false;
	pipelineStateDesc.DepthStencilState.StencilEnable = false;

	// 入力レイアウトの設定
	pipelineStateDesc.InputLayout.pInputElementDescs = inputLayouts;
	pipelineStateDesc.InputLayout.NumElements = _countof(inputLayouts);

	pipelineStateDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// レンダーターゲットの設定
	pipelineStateDesc.NumRenderTargets = 1;
	pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	// アンチエイリアシングの設定
	pipelineStateDesc.SampleDesc.Count = 1;
	pipelineStateDesc.SampleDesc.Quality = 0;

	// グラフィックスパイプラインステートの作成
	result = device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&pipelineState));
}

void DxUpdate()
{
	HRESULT result = S_OK;

	// 座標変換のための行列を計算
	XMMATRIX matrix = XMMatrixIdentity();
	// ワールド行列の角度を更新して画像を回転させる
	angle += 0.1f;
	matrix *= XMMatrixRotationY(angle);
	// ビュー行列の計算
	XMFLOAT3 eye(0.0f, 10.0f, -15.0f);
	XMFLOAT3 focus(0.0f, 10.0f, 0.0f);
	XMFLOAT3 up(0.0f, 1.0f, 0.0f);
	matrix *= XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&focus), XMLoadFloat3(&up));
	// プロジェクション行列の計算
	matrix *= XMMatrixPerspectiveFovLH(XM_PIDIV2, static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT), 1.0f, 100.0f);

	// マップして定数バッファーに行列を書き込み
	XMMATRIX* matrixMap = nullptr;
	result = constantBuffer->Map(0, NULL, (void**)&matrixMap);
	*matrixMap = matrix;
	constantBuffer->Unmap(0, nullptr);

	auto image = textureData.GetImage(0, 0, 0);

	// テクスチャバッファーに対してバリアを設定
	// ピクセルシェーダー用のリソースからテクスチャのコピー先に切り替え
	D3D12_RESOURCE_BARRIER barrierDesc = {};
	barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrierDesc.Transition.pResource = textureBuffer;
	barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	commandList->ResourceBarrier(1, &barrierDesc);

	// アップロード用のバッファーからテクスチャバッファーへテクスチャをコピー
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = uploadBuffer;
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Offset = 0;
	src.PlacedFootprint.Footprint.Width = textureMetaData.width;
	src.PlacedFootprint.Footprint.Height = textureMetaData.height;
	src.PlacedFootprint.Footprint.Depth = textureMetaData.depth;
	src.PlacedFootprint.Footprint.RowPitch = (image->rowPitch + 0xFF) & ~0xFF;
	src.PlacedFootprint.Footprint.Format = image->format;

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = textureBuffer;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	// コピーしたらピクセルシェーダー用のリソースに切り替え
	barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1, &barrierDesc);

	UINT index = swapChain->GetCurrentBackBufferIndex();

	ID3D12Resource* backBuffer = nullptr;
	result = swapChain->GetBuffer(index, IID_PPV_ARGS(&backBuffer));

	// バックバッファーに対してバリアを設定
	// 画面に表示しているバッファーから描画先に切り替え
	barrierDesc = {};
	barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrierDesc.Transition.pResource = backBuffer;
	barrierDesc.Transition.Subresource = 0;
	barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &barrierDesc);

	// グラフィックスパイプラインステートの設定
	commandList->SetPipelineState(pipelineState);

	// ルートシグネチャの設定
	commandList->SetGraphicsRootSignature(rootSignature);

	// ディスクリプタヒープの設定
	commandList->SetDescriptorHeaps(1, &basicHeaps);

	// ディスクリプタテーブルとしてのルートパラメーターとディスクリプタヒープの紐付け
	commandList->SetGraphicsRootDescriptorTable(0, basicHeaps->GetGPUDescriptorHandleForHeapStart());

	// レンダーターゲットの設定
	UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += index * stride;
	commandList->OMSetRenderTargets(1, &handle, true, nullptr);

	// ビューポートの設定
	D3D12_VIEWPORT viewport = {};
	viewport.Width = WINDOW_WIDTH;
	viewport.Height = WINDOW_HEIGHT;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MaxDepth = 1.0f;
	viewport.MinDepth = 0.0f;
	commandList->RSSetViewports(1, &viewport);

	// シザー矩形の設定
	D3D12_RECT scissorRect = {};
	scissorRect.top = 0;
	scissorRect.left = 0;
	scissorRect.bottom = scissorRect.top + WINDOW_HEIGHT;
	scissorRect.right = scissorRect.left + WINDOW_WIDTH;
	commandList->RSSetScissorRects(1, &scissorRect);

	// 画面のクリア
	float clearColor[] = { 1.0f, 0.0f, 0.0f, 1.0f };
	commandList->ClearRenderTargetView(handle, clearColor, 0, nullptr);

	// プリミティブトポロジーの設定
	// トライアングルリストを使用
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 頂点バッファービューの設定
	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vbView.SizeInBytes = PMD_VERTEX_SIZE * modelVertices.size();
	vbView.StrideInBytes = PMD_VERTEX_SIZE;
	commandList->IASetVertexBuffers(0, 1, &vbView);

	// インデックスバッファービューの設定
	D3D12_INDEX_BUFFER_VIEW ibView = {};
	ibView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	ibView.Format = DXGI_FORMAT_R16_UINT;
	ibView.SizeInBytes = sizeof(modelIndices[0]) * modelIndices.size();
	commandList->IASetIndexBuffer(&ibView);

	// 描画
	commandList->DrawIndexedInstanced(modelIndices.size(), 1, 0, 0, 0);

	// 描画したら画面表示バッファーへ切り替え
	barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &barrierDesc);

	// コマンドリストをクローズ
	commandList->Close();

	// コマンド実行
	ID3D12CommandList* commandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(1, commandLists);
	
	// フェンスでGPUの状態を確認しながら待機
	commandQueue->Signal(fence, ++fenceValue);
	if (fence->GetCompletedValue() != fenceValue) {
		auto event = CreateEvent(nullptr, false, false, nullptr);
		fence->SetEventOnCompletion(fenceValue, event);

		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}

	// コマンドをリセット
	commandAllocator->Reset();
	commandList->Reset(commandAllocator, nullptr);

	// 画面に表示するバッファーを切り替え
	swapChain->Present(1, 0);
}

LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (msg == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	WNDCLASSEX w = {};
	w.cbSize = sizeof(WNDCLASSEX);
	w.lpfnWndProc = (WNDPROC)WindowProcedure;
	w.lpszClassName = TEXT("DX12Sample");
	w.hInstance = GetModuleHandle(nullptr);
	RegisterClassEx(&w);

	RECT wrc = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	HWND hwnd = CreateWindow(
		w.lpszClassName,
		TEXT("DX12テスト"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		w.hInstance,
		nullptr
	);

	DxInitialize(hwnd);

	ShowWindow(hwnd, SW_SHOW);

	MSG msg = {};
	while (true) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (msg.message == WM_QUIT) {
			break;
		}

		DxUpdate();
	}

	UnregisterClass(w.lpszClassName, w.hInstance);
}
