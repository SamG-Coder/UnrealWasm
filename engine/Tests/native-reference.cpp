// Runs the unchanged, UE-preprocessed HLSL pass on Direct3D for a GPU oracle.
// No Lumen algorithm is reimplemented in this harness.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <string>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if(FAILED(hr)) throw std::runtime_error("Direct3D failure " + std::to_string(hr)); }
int main(int argc,char** argv) {
 try {
  if(argc!=4) throw std::runtime_error("Usage: native-reference shader.usf cases.bin results.bin");
  std::ifstream source(argv[1],std::ios::binary);
  std::string hlsl((std::istreambuf_iterator<char>(source)),{});
  if(hlsl.empty())throw std::runtime_error("Missing HLSL source");
  ComPtr<ID3DBlob> code,errors;
  HRESULT hr=D3DCompile(hlsl.data(),hlsl.size(),argv[1],nullptr,nullptr,"SetupAdaptiveProbeIndirectArgsCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
  if(FAILED(hr)&&errors)std::cerr<<(const char*)errors->GetBufferPointer();
  Check(hr);
  ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
  Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
  ComPtr<ID3D11ComputeShader> shader;Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
  auto buffer=[&](UINT bytes,UINT bind,UINT misc=0,UINT stride=0) {
   D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=bind;d.MiscFlags=misc;d.StructureByteStride=stride;
   ComPtr<ID3D11Buffer> b;Check(device->CreateBuffer(&d,nullptr,&b));return b;
  };
  auto constants=buffer(128,D3D11_BIND_CONSTANT_BUFFER),adaptive=buffer(4,D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,4),output=buffer(80,D3D11_BIND_UNORDERED_ACCESS);
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};srvDesc.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;srvDesc.Buffer.NumElements=1;
  ComPtr<ID3D11ShaderResourceView> srv;Check(device->CreateShaderResourceView(adaptive.Get(),&srvDesc,&srv));
  D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};uavDesc.Format=DXGI_FORMAT_R32_UINT;uavDesc.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uavDesc.Buffer.NumElements=20;
  ComPtr<ID3D11UnorderedAccessView> uav;Check(device->CreateUnorderedAccessView(output.Get(),&uavDesc,&uav));
  D3D11_BUFFER_DESC readDesc{};readDesc.ByteWidth=80;readDesc.Usage=D3D11_USAGE_STAGING;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> readback;Check(device->CreateBuffer(&readDesc,nullptr,&readback));
  context->CSSetShader(shader.Get(),nullptr,0);context->CSSetConstantBuffers(0,1,constants.GetAddressOf());
  context->CSSetShaderResources(0,1,srv.GetAddressOf());context->CSSetUnorderedAccessViews(0,1,uav.GetAddressOf(),nullptr);
  std::ifstream cases(argv[2],std::ios::binary);std::ofstream results(argv[3],std::ios::binary);
  uint32_t count=0;cases.read((char*)&count,4);if(!cases||count>10000)throw std::runtime_error("Invalid cases");
  for(uint32_t i=0;i<count;++i) {
   uint32_t data[33];cases.read((char*)data,sizeof(data));if(!cases)throw std::runtime_error("Truncated case");
   context->UpdateSubresource(constants.Get(),0,nullptr,data,0,0);context->UpdateSubresource(adaptive.Get(),0,nullptr,data+32,0,0);
   UINT sentinel[4]={0xcdcdcdcd,0xcdcdcdcd,0xcdcdcdcd,0xcdcdcdcd};context->ClearUnorderedAccessViewUint(uav.Get(),sentinel);
   context->Dispatch(1,1,1);context->CopyResource(readback.Get(),output.Get());
   D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
   results.write((char*)mapped.pData,80);context->Unmap(readback.Get(),0);
  }
  if(!results)throw std::runtime_error("Could not write results");
  std::cout<<"Native Direct3D shader cases: "<<count<<"\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
