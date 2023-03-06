/** @file Week4-6-ShapeComplete.cpp
 *  @brief Shape Practice Solution.
 *
 *  Place all of the scene geometry in one big vertex and index buffer.
 * Then use the DrawIndexedInstanced method to draw one object at a time ((as the
 * world matrix needs to be changed between objects)
 *
 *   Controls:
 *   Hold down '1' key to view scene in wireframe mode.
 *   Hold the left mouse button down and move the mouse to rotate.
 *   Hold the right mouse button down and move the mouse to zoom in and out.
 *
 *  @author Hooman Salamat
 */


#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

float rotation;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;

};

class ShapesApp : public D3DApp
{
public:
	ShapesApp(HINSTANCE hInstance);
	ShapesApp(const ShapesApp& rhs) = delete;
	ShapesApp& operator=(const ShapesApp& rhs) = delete;
	~ShapesApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	void MakeThing(std::string name, float scaleX, float scaleY, float scaleZ, float posX, float posY, float posZ, float pitch = 0, float yaw = 0, float roll = 0);

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
	UINT objectIndexnumber;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		ShapesApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	
	/*------------------------ LIGHTS ------------------------*/
	
	//Ambient

	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	

	//Directional/Parallel

	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.2f };
	
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };


	//Point

	mMainPassCB.Lights[3].Position = { -22.0f, 28.0f, 22.0f };
	mMainPassCB.Lights[3].Strength = { 1.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[3].FalloffStart = 20.0f;
	mMainPassCB.Lights[3].FalloffEnd = 28.0f;
	
	mMainPassCB.Lights[4].Position = { 22.0f, 28.0f, 22.0f };
	mMainPassCB.Lights[4].Strength = { 0.0f, 0.75f, 1.0f };
	mMainPassCB.Lights[4].FalloffStart = 20.0f;
	mMainPassCB.Lights[4].FalloffEnd = 28.0f;
	
	mMainPassCB.Lights[5].Position = { -22.0f, 28.0f, -22.0f };
	mMainPassCB.Lights[5].Strength = { 0.0f, 0.8f, 0.0f };
	mMainPassCB.Lights[5].FalloffStart = 20.0f;
	mMainPassCB.Lights[5].FalloffEnd = 28.0f;
	
	mMainPassCB.Lights[6].Position = { 22.0f, 28.0f, -22.0f };
	mMainPassCB.Lights[6].Strength = { 0.4f, 0.0f, 1.0f };
	mMainPassCB.Lights[6].FalloffStart = 20.0f;
	mMainPassCB.Lights[6].FalloffEnd = 28.0f;
	
	
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}



void ShapesApp::BuildRootSignature()
{
	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	// Create root CBV.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsConstantBufferView(2);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}


void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(100.0f, 100.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(1.0f, 1.0f, 3.0f, 20, 20);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f, 1);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f, 5);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 2.0f, 12, 4);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(2.0f, 1.0f, 12);
	GeometryGenerator::MeshData spike = geoGen.CreateSpike(2.0f, 3.0f, 1.0f, 6, 2);
	GeometryGenerator::MeshData squarewindow = geoGen.CreateSquareWindow(0.5f, 1.0f, 1.0f);
	GeometryGenerator::MeshData caltrop = geoGen.CreateCaltrop(1.0f, 1.0f, 1.0f);


	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.


	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT wedgeVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT pyramidVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT coneVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT diamondVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT spikeVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
	UINT squarewindowVertexOffset = spikeVertexOffset + (UINT)spike.Vertices.size();
	UINT caltropVertexOffset = squarewindowVertexOffset + (UINT)squarewindow.Vertices.size();


	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT wedgeIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT pyramidIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT coneIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT diamondIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT spikeIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
	UINT squarewindowIndexOffset = spikeIndexOffset + (UINT)spike.Indices32.size();
	UINT caltropIndexOffset = squarewindowIndexOffset + (UINT)squarewindow.Indices32.size();

	// Define the SubmeshGeometry that cover different
	// regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry spikeSubmesh;
	spikeSubmesh.IndexCount = (UINT)spike.Indices32.size();
	spikeSubmesh.StartIndexLocation = spikeIndexOffset;
	spikeSubmesh.BaseVertexLocation = spikeVertexOffset;

	SubmeshGeometry squarewindowSubmesh;
	squarewindowSubmesh.IndexCount = (UINT)squarewindow.Indices32.size();
	squarewindowSubmesh.StartIndexLocation = squarewindowIndexOffset;
	squarewindowSubmesh.BaseVertexLocation = squarewindowVertexOffset;

	SubmeshGeometry caltropSubmesh;
	caltropSubmesh.IndexCount = (UINT)caltrop.Indices32.size();
	caltropSubmesh.StartIndexLocation = caltropIndexOffset;
	caltropSubmesh.BaseVertexLocation = caltropVertexOffset;
	

	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		wedge.Vertices.size() +
		pyramid.Vertices.size() +
		cone.Vertices.size() +
		diamond.Vertices.size() +
		spike.Vertices.size() + 
		squarewindow.Vertices.size() +
		caltrop.Vertices.size();


	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;

	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
	}

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Normal = wedge.Vertices[i].Normal;
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
	}

	for (size_t i = 0; i < spike.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = spike.Vertices[i].Position;
		vertices[k].Normal = spike.Vertices[i].Normal;
	}

	for (size_t i = 0; i < squarewindow.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = squarewindow.Vertices[i].Position;
		vertices[k].Normal = squarewindow.Vertices[i].Normal;
	}
	
	for (size_t i = 0; i < caltrop.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = caltrop.Vertices[i].Position;
		vertices[k].Normal = caltrop.Vertices[i].Normal;
	}

	

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(spike.GetIndices16()), std::end(spike.GetIndices16()));
	indices.insert(indices.end(), std::begin(squarewindow.GetIndices16()), std::end(squarewindow.GetIndices16()));
	indices.insert(indices.end(), std::begin(caltrop.GetIndices16()), std::end(caltrop.GetIndices16()));


	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";


	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["spike"] = spikeSubmesh;
	geo->DrawArgs["squarewindow"] = squarewindowSubmesh;
	geo->DrawArgs["caltrop"] = caltropSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)));
}



void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
	}
}

void ShapesApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(Colors::ForestGreen);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(Colors::LightSteelBlue);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(Colors::LightGray);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.5f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 3;
	skullMat->DiffuseSrvHeapIndex = 3;
	skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skullMat->Roughness = 0.3f;

	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["skullMat"] = std::move(skullMat);
}

//CREATED FUNCTION FOR RENDERING OBJECTS TO MAKE IT EASIER INTO THE ShapesApp::BuildRenderItems() function.
void ShapesApp::MakeThing(std::string name, float scaleX, float scaleY, float scaleZ, float posX, float posY, float posZ, float pitch, float yaw, float roll)
{
	auto item = std::make_unique<RenderItem>();
	
	XMStoreFloat4x4(&item->World, XMMatrixScaling(scaleX, scaleY, scaleZ) * XMMatrixTranslation(posX, posY, posZ) * XMMatrixRotationRollPitchYaw(pitch, yaw, roll));
	XMStoreFloat4x4(&item->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	item->ObjCBIndex = objectIndexnumber;

	item->Mat = mMaterials["stone0"].get();
	item->Geo = mGeometries["shapeGeo"].get();
	item->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	item->IndexCount = item->Geo->DrawArgs[name].IndexCount;
	item->StartIndexLocation = item->Geo->DrawArgs[name].StartIndexLocation;
	item->BaseVertexLocation = item->Geo->DrawArgs[name].BaseVertexLocation;

	mAllRitems.push_back(std::move(item));

	objectIndexnumber++;
}


void ShapesApp::BuildRenderItems()
{
	/*-------------------- GRASSY GROUND --------------------*/
	MakeThing("grid", 10.0f, 1.0f, 1.0f, 0.0f, 0.0f, 20.0f); 
	MakeThing("grid", 10.0f, 1.0f, 1.0f, 0.0f, 0.0f, -110.0f);
	MakeThing("box", 1000.0f, 1.0f, 50.0f, 0.0f, -0.525f, -40.0f);


	/*-------------------- CASTLE WALLS -------------------*/
	MakeThing("box", 50.0f, 15.0f, 3.0f, 0.0f, 7.5f, 25.0f); //back wall
	MakeThing("box", 3.0f, 15.0f, 50.0f, 25.0f, 7.5f, 0.0f); //right wall
	MakeThing("box", 3.0f, 15.0f, 50.0f, -25.0f, 7.5f, 0.0f); //left wall
	MakeThing("box", 50.0f, 15.0f, 3.0f, 0.0f, 7.5f, -25.0f); //front wall

	/*-------------------- CASTLE CORNERS -------------------*/
	MakeThing("cylinder", 5.0f, 6.0f, 5.0f, -25.0f, 8.5f, 25.0f); //Back Left
	MakeThing("cylinder", 5.0f, 6.0f, 5.0f, 25.0f, 8.5f, 25.0f); //Back Right
	MakeThing("cylinder", 5.0f, 6.0f, 5.0f, -25.0f, 8.5f, -25.0f); //Front Left
	MakeThing("cylinder", 5.0f, 6.0f, 5.0f, 25.0f, 8.5f, -25.0f); //Front Right

	/*-------------------- CASTLE CORNER TOPS -------------------*/
	MakeThing("cone", 6.5f, 4.5f, 6.5f, -25.0f, 21.0f, 25.0f); //Back Left
	MakeThing("cone", 6.5f, 4.5f, 6.5f, 25.0f, 21.0f, 25.0f); //Back Right
	MakeThing("cone", 6.5f, 4.5f, 6.5f, -25.0f, 21.0f, -25.0f); //Front Left
	MakeThing("cone", 6.5f, 4.5f, 6.5f, 25.0f, 21.0f, -25.0f); //Front Right

	/*-------------------- CASTLE DOOR -------------------*/
	MakeThing("squarewindow", 10.0f, 10.0f, 10.0f, 0.0f, 7.5f, -25.0f);

	/*-------------------- DIAMOND & PEDESTAL -------------------*/
	MakeThing("box", 1.0f, 5.0f, 1.0f, 0.0f, 0.0f, 10.0f);
	MakeThing("diamond", 1.0f, 2.5f, 1.0f, 0.0f, 4.0f, 10.0f);

	/*-------------------- CALTROPS -------------------*/
	MakeThing("caltrop", 0.7f, 0.7f, 0.7f, -2.0f, 0.325f, 8.0f);
	MakeThing("caltrop", 0.7f, 0.7f, 0.7f, 2.0f, 0.325f, 7.2f);
	MakeThing("caltrop", 0.7f, 0.7f, 0.7f, -1.8f, 0.325f, 10.0f);
	MakeThing("caltrop", 0.7f, 0.7f, 0.7f, 0.0f, 0.325f, 7.0f);
	MakeThing("caltrop", 0.7f, 0.7f, 0.7f, 0.6f, 0.325f, 11.0f);
	MakeThing("caltrop", 0.7f, 0.7f, 0.7f, -0.3f, 0.325f, 14.0f);
	MakeThing("caltrop", 0.7f, 0.7f, 0.7f, 4.0f, 0.325f, 10.5f);
	
	//right side spikes
	MakeThing("spike", 0.6f, 8.0f, 0.6f, 6.0f, 0.0f, -28.0f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, 6.0f, 0.0f, -30.25f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, 6.0f, 0.0f, -32.5f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, 6.0f, 0.0f, -34.75f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, 6.0f, 0.0f, -37.0f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, 6.0f, 0.0f, -39.25f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, 6.0f, 0.0f, -41.5f);
	
	//left side spikes
	MakeThing("spike", 0.6f, 8.0f, 0.6f, -6.0f, 0.0f, -28.0f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, -6.0f, 0.0f, -30.25f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, -6.0f, 0.0f, -32.5f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, -6.0f, 0.0f, -34.75f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, -6.0f, 0.0f, -37.0f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, -6.0f, 0.0f, -39.25f);
	MakeThing("spike", 0.6f, 8.0f, 0.6f, -6.0f, 0.0f, -41.5f);

	/*-------------------- CASTLE WINDOWS -------------------*/
	MakeThing("squarewindow", 2.0f, 2.0f, 7.0f, 12.5f, 7.5f, 25.0f);
	MakeThing("squarewindow", 2.0f, 2.0f, 7.0f, -12.5f, 7.5f, 25.0f);
	MakeThing("squarewindow", 2.0f, 2.0f, 7.0f, 12.5f, 7.5f, 25.0f, 0.0f, 90 * (XM_PI / 180));
	MakeThing("squarewindow", 2.0f, 2.0f, 7.0f, -12.5f, 7.5f, 25.0f, 0.0f, 90 * (XM_PI / 180));
	MakeThing("squarewindow", 2.0f, 2.0f, 7.0f, 12.5f, 7.5f, -25.0f, 0.0f, 90 * (XM_PI / 180));
	MakeThing("squarewindow", 2.0f, 2.0f, 7.0f, -12.5f, 7.5f, -25.0f, 0.0f, 90 * (XM_PI / 180));

	/*-------------------- CASTLE DRAWBRIDGE -------------------*/
	MakeThing("wedge", 5.0f, 20.0f, 10.0f, 0.0f, 35.0f, 0.0f, 0, -90 * (XM_PI / 180), 90 * (XM_PI/180));
	MakeThing("wedge", 5.0f, 20.0f, 10.0f, 0.0f, -55.1f, 0.0f, 0, 90 * (XM_PI / 180), 90 * (XM_PI / 180));

	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(1, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

