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
#include "../../Common/Camera.h"
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

	std::string name;
	BoundingBox box;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTestedTreeSprites,
	Count
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
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void Collision();

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildTreeSpritesGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();



	void MakeThing(std::string name, std::string material, RenderLayer type, XMFLOAT3 objectScale, XMFLOAT3 objectPos, XMFLOAT2 textureScale, XMFLOAT3 ObjectRotation = XMFLOAT3(0,0,0));

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
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	PassConstants mMainPassCB;

	Camera mCamera;
	BoundingBox player;


	POINT mLastMousePos;
	UINT objectIndexnumber = 0;
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

	mCamera.SetPosition(0.0f, 3.0f, -150.0f);

	player.Center = mCamera.GetPosition3f();
	player.Extents = XMFLOAT3(1.5f, 0.6f, 1.5f);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildTreeSpritesGeometry();
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
	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void ShapesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

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

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);

	Collision();
}

void ShapesApp::Draw(const GameTimer& gt)
{
	/*------------* DRAW OPAQUE OBJECTS *------------*/

	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);

	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);


	/*------------* DRAW TREE BILLBOARDS *------------*/

	mCommandList->SetPipelineState(mPSOs["tree"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	/*------------* DRAW TRANSLUCENT OBJECTS *------------*/

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

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

		//step4: Instead of updating the angles based on input to orbit camera around scene, 
		//we rotate the camera’s look direction:
		//mTheta += dx;
		//mPhi += dy;

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	//GetAsyncKeyState returns a short (2 bytes)
	if (GetAsyncKeyState('W') & 0x8000) //most significant bit (MSB) is 1 when key is pressed (1000 000 000 000)
		mCamera.Walk(10.0f * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f * dt);

	if (GetAsyncKeyState('P') & 0x8000)
		mCamera.Pedestal(10.0f * dt);

	if (GetAsyncKeyState('O') & 0x8000)
		mCamera.Pedestal(-10.0f * dt);

	mCamera.SetPosition(mCamera.GetPosition3f().x, 3.0f, mCamera.GetPosition3f().z);
	player.Center = mCamera.GetPosition3f();

	mCamera.UpdateViewMatrix();
}

void ShapesApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.01f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if (tu >= 1.0f)
		tu -= 1.0f;
	if (tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
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
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

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
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	
	mMainPassCB.FogColor = XMFLOAT4(0.125f, 0.26f, 0.3f, 0.5f);
	mMainPassCB.gFogStart = 70.0f;

	/*------------------------ LIGHTS ------------------------*/
	
	//Ambient
	mMainPassCB.AmbientLight = { 0.01f, 0.01f, 0.01f, 0.5f };


	//Directional/Parallel

	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.2f, 0.2f, 0.066f };
	
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.1f, 0.1f, 0.1f };
	
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.045f, 0.045f, 0.045f };


	//Point

	mMainPassCB.Lights[3].Position = { -22.0f, 28.0f, 22.0f };
	mMainPassCB.Lights[3].Strength = { 1.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[3].FalloffStart = 20.0f;
	mMainPassCB.Lights[3].FalloffEnd = 35.0f;
	
	mMainPassCB.Lights[4].Position = { 22.0f, 28.0f, 22.0f };
	mMainPassCB.Lights[4].Strength = { 0.0f, 0.75f, 1.0f };
	mMainPassCB.Lights[4].FalloffStart = 20.0f;
	mMainPassCB.Lights[4].FalloffEnd = 35.0f;
	
	mMainPassCB.Lights[5].Position = { -22.0f, 28.0f, -22.0f };
	mMainPassCB.Lights[5].Strength = { 0.0f, 0.8f, 0.0f };
	mMainPassCB.Lights[5].FalloffStart = 20.0f;
	mMainPassCB.Lights[5].FalloffEnd = 35.0f;
	
	mMainPassCB.Lights[6].Position = { 22.0f, 28.0f, -22.0f };
	mMainPassCB.Lights[6].Strength = { 0.4f, 0.0f, 1.0f };
	mMainPassCB.Lights[6].FalloffStart = 20.0f;
	mMainPassCB.Lights[6].FalloffEnd = 35.0f;
	

	//Torch

	mMainPassCB.Lights[7].Position = mCamera.GetPosition3f();
	mMainPassCB.Lights[7].Strength = { 0.7f, 0.45f, 0.0f };
	mMainPassCB.Lights[7].FalloffStart = 25.0f;
	mMainPassCB.Lights[7].FalloffEnd = 50.0f;

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

float Sign(const float value)
{
	return (value < 0.0f) ? -1.0f : 1.0f;
}

void ShapesApp::Collision()
{
	for (int i = 0; i < mAllRitems.size(); i++)
	{
		if (mAllRitems[i]->name == "box")
		{

			float distX = mAllRitems[i]->box.Center.x - player.Center.x;
			float distZ = mAllRitems[i]->box.Center.z - player.Center.z;

			float sumX = player.Extents.x + mAllRitems[i]->box.Extents.x;
			float sumZ = player.Extents.z + mAllRitems[i]->box.Extents.z;

			float overX = sumX - abs(distX);
			float overZ = sumZ - abs(distZ);

			if (overX < 0 || overZ < 0)
			{
				continue;
			}

			XMFLOAT2 contact_normal;
			XMFLOAT3 min_trans;

			if (overX < overZ)
			{
				contact_normal = XMFLOAT2(Sign(distX), 0.0f);
				min_trans = XMFLOAT3(contact_normal.x * overX, 0.0f, 0.0f);
			}
			else
			{
				contact_normal = XMFLOAT2(0.0f, Sign(distZ));
				min_trans = XMFLOAT3(0.0f, 0.0f, contact_normal.y * overZ);
			}

			mCamera.SetPosition(mCamera.GetPosition3f().x - min_trans.x, mCamera.GetPosition3f().y - min_trans.y, mCamera.GetPosition3f().z - min_trans.z);
		}
	}
}


void ShapesApp::LoadTextures()
{
	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"../../Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"../../Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	auto fenceTex = std::make_unique<Texture>();
	fenceTex->Name = "fenceTex";
	fenceTex->Filename = L"../../Textures/bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), fenceTex->Filename.c_str(),
		fenceTex->Resource, fenceTex->UploadHeap));

	auto woodTex = std::make_unique<Texture>();
	woodTex->Name = "woodTex";
	woodTex->Filename = L"../../Textures/wood.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), woodTex->Filename.c_str(),
		woodTex->Resource, woodTex->UploadHeap));
	
	auto iceTex = std::make_unique<Texture>();
	iceTex->Name = "iceTex";
	iceTex->Filename = L"../../Textures/ice.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), iceTex->Filename.c_str(),
		iceTex->Resource, iceTex->UploadHeap));

	auto metalTex = std::make_unique<Texture>();
	metalTex->Name = "metalTex";
	metalTex->Filename = L"../../Textures/metal.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), metalTex->Filename.c_str(),
		metalTex->Resource, metalTex->UploadHeap));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"../../Textures/treeArray2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));

	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[fenceTex->Name] = std::move(fenceTex);
	mTextures[woodTex->Name] = std::move(woodTex);
	mTextures[iceTex->Name] = std::move(iceTex);
	mTextures[metalTex->Name] = std::move(metalTex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[6];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);
	slotRootParameter[4].InitAsConstantBufferView(3);
	slotRootParameter[5].InitAsConstantBufferView(4);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

void ShapesApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 7; //
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto fenceTex = mTextures["fenceTex"]->Resource;
	auto woodTex = mTextures["woodTex"]->Resource;
	auto iceTex = mTextures["iceTex"]->Resource;
	auto metalTex = mTextures["metalTex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = fenceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, hDescriptor);

	//Wood descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = woodTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(woodTex.Get(), &srvDesc, hDescriptor);
	
	//ice descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = iceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

	//metal descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = metalTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(metalTex.Get(), &srvDesc, hDescriptor);

	//tree descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);

}

void ShapesApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO defines[] =
	{
		//"NOFOG",
			"FOG", 
		"1", NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_1");

	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;

	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;

	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;

	}

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Normal = wedge.Vertices[i].Normal;
		vertices[k].TexC = wedge.Vertices[i].TexC;

	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
		vertices[k].TexC = pyramid.Vertices[i].TexC;

	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
		vertices[k].TexC = cone.Vertices[i].TexC;

	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
		vertices[k].TexC = diamond.Vertices[i].TexC;

	}

	for (size_t i = 0; i < spike.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = spike.Vertices[i].Position;
		vertices[k].Normal = spike.Vertices[i].Normal;
		vertices[k].TexC = spike.Vertices[i].TexC;

	}

	for (size_t i = 0; i < squarewindow.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = squarewindow.Vertices[i].Position;
		vertices[k].Normal = squarewindow.Vertices[i].Normal;
		vertices[k].TexC = squarewindow.Vertices[i].TexC;

	}
	
	for (size_t i = 0; i < caltrop.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = caltrop.Vertices[i].Position;
		vertices[k].Normal = caltrop.Vertices[i].Normal;
		vertices[k].TexC = caltrop.Vertices[i].TexC;

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

void ShapesApp::BuildTreeSpritesGeometry()
{
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	static const int treeCount = 12;
	std::array<TreeSpriteVertex, 12> vertices;
	for (UINT i = 0; i < treeCount; ++i)
	{
		vertices[i].Size = XMFLOAT2(10.0f, 10.0f);
	}

	vertices[0].Pos = XMFLOAT3(45.0f, 4.0f, 35.0f);
	vertices[1].Pos = XMFLOAT3(45.0f, 4.0f, 25.0f);
	vertices[2].Pos = XMFLOAT3(45.0f, 4.0f, 15.0f);
	vertices[3].Pos = XMFLOAT3(45.0f, 4.0f, 5.0f);
	vertices[4].Pos = XMFLOAT3(45.0f, 4.0f, -5.0f);
	vertices[5].Pos = XMFLOAT3(-45.0f, 4.0f, -15.0f);
	vertices[6].Pos = XMFLOAT3(-45.0f, 4.0f, 35.0f);
	vertices[7].Pos = XMFLOAT3(-45.0f, 4.0f, 25.0f);
	vertices[8].Pos = XMFLOAT3(-45.0f, 4.0f, 15.0f);
	vertices[9].Pos = XMFLOAT3(-45.0f, 4.0f, 5.0f);
	vertices[10].Pos = XMFLOAT3(-45.0f, 4.0f, -5.0f);
	vertices[11].Pos = XMFLOAT3(-45.0f, 4.0f, -15.0f);

	std::array<std::uint16_t, 12> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}

void ShapesApp::BuildPSOs()
{
	/*----------- OPAQUE OBJECTS -----------*/
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
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
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));



	/*----------- TRANSLUCENT OBJECTS -----------*/
	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	/*----------- TREE BILLBOARD OBJECTS -----------*/
	
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treePsoDesc = transparentPsoDesc;

	treePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treePsoDesc.pRootSignature = mRootSignature.Get();
	treePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};

	treePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};

	treePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	treePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	treePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	treePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	treePsoDesc.SampleMask = UINT_MAX;
	treePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treePsoDesc.NumRenderTargets = 1;
	treePsoDesc.RTVFormats[0] = mBackBufferFormat;
	//there is abug with F2 key that is supposed to turn on the multisampling!
	//Set4xMsaaState(true);
	//m4xMsaaState = true;

	treePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	treePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	treePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treePsoDesc, IID_PPV_ARGS(&mPSOs["tree"])));

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
	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.6f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefence";
	wirefence->MatCBIndex = 2;
	wirefence->DiffuseSrvHeapIndex = 2;
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;


	auto wood = std::make_unique<Material>();
	wood->Name = "wood";
	wood->MatCBIndex = 3;
	wood->DiffuseSrvHeapIndex = 3;
	wood->DiffuseAlbedo = XMFLOAT4(Colors::SandyBrown);
	wood->FresnelR0 = XMFLOAT3(0.15f, 0.18f, 0.18f);
	wood->Roughness = 0.25f;

	auto ice = std::make_unique<Material>();
	ice->Name = "ice";
	ice->MatCBIndex = 4;
	ice->DiffuseSrvHeapIndex = 4;
	ice->DiffuseAlbedo = XMFLOAT4(Colors::LightBlue);
	ice->FresnelR0 = XMFLOAT3(0.15f, 0.18f, 0.18f);
	ice->Roughness = 0.25f;

	auto metal = std::make_unique<Material>();
	metal->Name = "metal";
	metal->MatCBIndex = 5;
	metal->DiffuseSrvHeapIndex = 5;
	metal->DiffuseAlbedo = XMFLOAT4(Colors::DarkGray);
	metal->FresnelR0 = XMFLOAT3(0.15f, 0.18f, 0.18f);
	metal->Roughness = 0.25f;

	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = 6;
	treeSprites->DiffuseSrvHeapIndex = 6;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;


	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["wirefence"] = std::move(wirefence);
	mMaterials["wood"] = std::move(wood);
	mMaterials["ice"] = std::move(ice);
	mMaterials["metal"] = std::move(metal);
	mMaterials["treeSprites"] = std::move(treeSprites);
}

//CREATED FUNCTION FOR RENDERING OBJECTS TO MAKE IT EASIER INTO THE ShapesApp::BuildRenderItems() function.
void ShapesApp::MakeThing(std::string name, std::string material, RenderLayer type, XMFLOAT3 objectScale, XMFLOAT3 objectPos, XMFLOAT2 textureScale, XMFLOAT3 ObjectRotation)
{
	auto item = std::make_unique<RenderItem>();

	item->name = name;


	//Collision for maze, detected if the shape is a box and if the material is a "wirefence" (the brick material we made)
	if (name == "box" && material == "wirefence")
	{
		item->box.Center = objectPos;
		item->box.Extents = XMFLOAT3(objectScale.x * 0.5f, objectScale.y * 0.5f, objectScale.z * 0.5);
	}
	
	XMStoreFloat4x4(&item->World, XMMatrixScaling(objectScale.x, objectScale.y, objectScale.z) * XMMatrixRotationRollPitchYaw(ObjectRotation.x * (XM_PI / 180), ObjectRotation.y * (XM_PI / 180), ObjectRotation.z * (XM_PI / 180)) * XMMatrixTranslation(objectPos.x, objectPos.y, objectPos.z));
	XMStoreFloat4x4(&item->TexTransform, XMMatrixScaling(textureScale.x, textureScale.y, 1.0f));
	item->ObjCBIndex = objectIndexnumber;

	item->Mat = mMaterials[material].get();
	item->Geo = mGeometries["shapeGeo"].get();
	item->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	item->IndexCount = item->Geo->DrawArgs[name].IndexCount;
	item->StartIndexLocation = item->Geo->DrawArgs[name].StartIndexLocation;
	item->BaseVertexLocation = item->Geo->DrawArgs[name].BaseVertexLocation;

	mRitemLayer[(int)type].push_back(item.get());
	mAllRitems.push_back(std::move(item));

	objectIndexnumber++;
}


void ShapesApp::BuildRenderItems()
{
	objectIndexnumber = 0;

	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = objectIndexnumber;
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();

	treeSpritesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());

	mAllRitems.push_back(std::move(treeSpritesRitem));
	
	objectIndexnumber++;

	/*-------------------- MAZE --------------------*/

	/*-------------------- GRASSY GROUND --------------------*/
	MakeThing("box", "grass", RenderLayer::Opaque, { 300.0f, 10.0f, 100.0f }, { 0.0f, -5.0f, 20.0f }, { 150.0f, 50.0f });
	MakeThing("box", "grass", RenderLayer::Opaque, { 300.0f, 10.0f, 100.0f }, { 0.0f, -5.0f, -110.0f }, { 150.0f, 50.0f });
	MakeThing("box", "grass", RenderLayer::Opaque, { 300.0f, 5.0f, 100.0f }, { 0.0f, -10.0f, -45.0f }, { 150.0f, 50.0f });
	MakeThing("grid", "water", RenderLayer::Transparent, { 10.0f, 1.0f, 10.0f }, { 0.0f, -0.2f, -45.0f }, { 5.0f, 5.0f });

	/*-------------------- CASTLE WALLS -------------------*/
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 50.0f, 15.0f, 3.0f }, { 0.0f, 7.5f, 25.0f }, { 5.0f, 5.0f }); //back wall
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 3.0f, 15.0f, 50.0f }, { 25.0f, 7.5f, 0.0f }, { 5.0f, 5.0f }); //right wall
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 3.0f, 15.0f, 50.0f }, { -25.0f, 7.5f, 0.0f }, { 5.0f, 5.0f }); //left wall
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 50.0f, 15.0f, 3.0f }, { 0.0f, 7.5f, -25.0f }, { 5.0f, 5.0f }); //front wall

	/*-------------------- CASTLE CORNERS -------------------*/
	MakeThing("cylinder", "wirefence", RenderLayer::Opaque, { 5.0f, 6.0f, 5.0f }, { -25.0f, 8.5f, 25.0f }, { 5.0f, 5.0f }); //Back Left
	MakeThing("cylinder", "wirefence", RenderLayer::Opaque, { 5.0f, 6.0f, 5.0f }, { 25.0f, 8.5f, 25.0f }, { 5.0f, 5.0f }); //Back Right
	MakeThing("cylinder", "wirefence", RenderLayer::Opaque, { 5.0f, 6.0f, 5.0f }, { -25.0f, 8.5f, -25.0f }, { 5.0f, 5.0f }); //Front Left
	MakeThing("cylinder", "wirefence", RenderLayer::Opaque, { 5.0f, 6.0f, 5.0f }, { 25.0f, 8.5f, -25.0f }, { 5.0f, 5.0f }); //Front Right

	/*-------------------- CASTLE CORNER TOPS -------------------*/
	MakeThing("cone", "wirefence", RenderLayer::Opaque, { 6.5f, 4.5f, 6.5f }, { -25.0f, 21.0f, 25.0f }, { 5.0f, 5.0f }); //Back Left
	MakeThing("cone", "wirefence", RenderLayer::Opaque, { 6.5f, 4.5f, 6.5f }, { 25.0f, 21.0f, 25.0f }, { 5.0f, 5.0f }); //Back Right
	MakeThing("cone", "wirefence", RenderLayer::Opaque, { 6.5f, 4.5f, 6.5f }, { -25.0f, 21.0f, -25.0f }, { 5.0f, 5.0f }); //Front Left
	MakeThing("cone", "wirefence", RenderLayer::Opaque, { 6.5f, 4.5f, 6.5f }, { 25.0f, 21.0f, -25.0f }, { 5.0f, 5.0f }); //Front Right

	/*-------------------- CASTLE DOOR -------------------*/
	MakeThing("squarewindow", "metal", RenderLayer::Opaque, { 10.0f, 10.0f, 10.0f }, { 0.0f, 7.5f, -25.0f }, { 5.0f, 5.0f });

	/*-------------------- DIAMOND & PEDESTAL -------------------*/
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 5.0f, 1.0f }, { 0.0f, 0.0f, 10.0f }, { 5.0f, 5.0f });
	MakeThing("diamond", "ice", RenderLayer::Opaque, { 1.0f, 2.5f, 1.0f }, { 0.0f, 4.0f, 10.0f }, { 5.0f, 5.0f });

	/*-------------------- CALTROPS -------------------*/
	MakeThing("caltrop", "metal", RenderLayer::Opaque, { 0.7f, 0.7f, 0.7f }, { -2.0f, 0.325f, 8.0f }, { 5.0f, 5.0f });
	MakeThing("caltrop", "metal", RenderLayer::Opaque, { 0.7f, 0.7f, 0.7f }, { 2.0f, 0.325f, 7.2f }, { 5.0f, 5.0f });
	MakeThing("caltrop", "metal", RenderLayer::Opaque, { 0.7f, 0.7f, 0.7f }, { -1.8f, 0.325f, 10.0f }, { 5.0f, 5.0f });
	MakeThing("caltrop", "metal", RenderLayer::Opaque, { 0.7f, 0.7f, 0.7f }, { 0.0f, 0.325f, 7.0f }, { 5.0f, 5.0f });
	MakeThing("caltrop", "metal", RenderLayer::Opaque, { 0.7f, 0.7f, 0.7f }, { 0.6f, 0.325f, 11.0f }, { 5.0f, 5.0f });
	MakeThing("caltrop", "metal", RenderLayer::Opaque, { 0.7f, 0.7f, 0.7f }, { -0.3f, 0.325f, 14.0f }, { 5.0f, 5.0f });
	MakeThing("caltrop", "metal", RenderLayer::Opaque, { 0.7f, 0.7f, 0.7f }, { 4.0f, 0.325f, 10.5f }, { 5.0f, 5.0f });

	//right side spikes
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { 6.0f, 0.0f, -28.0f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { 6.0f, 0.0f, -30.25f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { 6.0f, 0.0f, -32.5f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { 6.0f, 0.0f, -34.75f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { 6.0f, 0.0f, -37.0f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { 6.0f, 0.0f, -39.25f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { 6.0f, 0.0f, -41.5f }, { 5.0f, 5.0f });

	//left side spikes
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { -6.0f, 0.0f, -28.0f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { -6.0f, 0.0f, -30.25f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { -6.0f, 0.0f, -32.5f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { -6.0f, 0.0f, -34.75f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { -6.0f, 0.0f, -37.0f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { -6.0f, 0.0f, -39.25f }, { 5.0f, 5.0f });
	MakeThing("spike", "wood", RenderLayer::Opaque, { 0.6f, 8.0f, 0.6f }, { -6.0f, 0.0f, -41.5f }, { 5.0f, 5.0f });

	/*-------------------- CASTLE DRAWBRIDGE -------------------*/
	MakeThing("wedge", "wood", RenderLayer::Opaque, { 5.0f, 20.0f, 10.0f }, { 0.0f, -2.0f, -35.0f }, { 5.0f, 5.0f }, { 0.0f, -90.0f, 90.0f });
	MakeThing("wedge", "wood", RenderLayer::Opaque, { 5.0f, 20.0f, 10.0f }, { 0.0f, -2.0f, -55.1f }, { 5.0f, 5.0f }, { 0.0f, 90.0f, 90.0f });

	/*------------------------ HEDGE MAZE ----------------------*/
	MakeThing("box", "wirefence", RenderLayer::Opaque, {1.0f, 15.0f, 100.0f}, {-25.0f, 7.5f, -110.0f}, {5.0f, 5.0f}); //left outer wall
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 100.0f }, { 25.0f, 7.5f, -110.0f }, { 5.0f, 5.0f }); //right outer wall
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 20.0f, 15.0f, 1.0f }, { -15.0f, 7.5f, -160.0f }, { 5.0f, 5.0f }); //back left outer wall
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 20.0f, 15.0f, 1.0f }, { 15.0f, 7.5f, -160.0f }, { 5.0f, 5.0f }); //back right outer wall
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 20.0f, 15.0f, 1.0f }, { -15.0f, 7.5f, -60.0f }, { 5.0f, 5.0f }); //outer wall closest to castle, left side
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 20.0f, 15.0f, 1.0f }, { 15.0f, 7.5f, -60.0f }, { 5.0f, 5.0f }); //outer wall closest to castle, right side

	/*--------------------- INNER HEDGE MAZE -------------------*/
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 10.0f }, { -5.5f, 7.5f, -155.0f }, { 5.0f, 5.0f }); //entrance left wall
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 10.0f }, { 5.5f, 7.5f, -155.0f }, { 5.0f, 5.0f }); //entrance right wall

	MakeThing("box", "wirefence", RenderLayer::Opaque, { 10.0f, 15.0f, 1.0f }, { -10.0f, 7.5f, -150.0f }, { 5.0f, 5.0f }); //1
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 50.0f }, { -20.0f, 7.5f, -135.0f }, { 5.0f, 5.0f }); //2
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 30.0f, 15.0f, 1.0f }, { -5.5f, 7.5f, -120.0f }, { 5.0f, 5.0f }); //3
	
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 20.0f, 15.0f, 1.0f }, { -2.25f, 7.5f, -130.0f }, { 5.0f, 5.0f }); //4 
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 12.5f }, { 7.5f, 7.5f, -135.7f }, { 5.0f, 5.0f }); // 5
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 20.0f, 15.0f, 1.0f }, { 15.0f, 7.5f, -141.5f }, { 5.0f, 5.0f }); // 6

	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 10.0f }, { 10.0f, 7.5f, -114.5f }, { 5.0f, 5.0f }); // 7
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 25.0f }, { 17.5f, 7.5f, -130.0f }, { 5.0f, 5.0f }); // 8
	//MakeThing("box", "wirefence", RenderLayer::Opaque, { 15.0f, 15.0f, 1.0f }, { 5.0f, 7.5f, -115.5f }, { 5.0f, 5.0f }); // 9

	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 10.0f }, { -3.0f, 7.5f, -105.0f }, { 5.0f, 5.0f }); // 10
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 27.5f, 15.0f, 1.0f }, { 11.0f, 7.5f, -102.5f }, { 5.0f, 5.0f }); // 11
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 30.0f, 15.0f, 1.0f }, { -10.0f, 7.5f, -90.0f }, { 5.0f, 5.0f }); // 12

	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 20.0f }, { 15.0f, 7.5f, -85.0f }, { 5.0f, 5.0f }); // 13
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 35.0f, 15.0f, 1.0f }, {7.5f, 7.5f, -75.0f }, { 5.0f, 5.0f }); // 14
	MakeThing("box", "wirefence", RenderLayer::Opaque, { 1.0f, 15.0f, 10.0f }, {-5.0f, 7.5f, -65.0f }, { 5.0f, 5.0f }); // 15

	MakeThing("box", "wirefence", RenderLayer::Opaque, { 10.0f, 15.0f, 1.0f }, {-2.5f, 7.5f, -70.0f }, { 5.0f, 5.0f }); // 16

}
// std::string name, std::string material, RenderLayer type, XMFLOAT3 objectScale, XMFLOAT3 objectPos, XMFLOAT2 textureScale, XMFLOAT3 ObjectRotation

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

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> ShapesApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}