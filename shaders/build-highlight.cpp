#include "backend/machine_ir.hpp"
#include "gxp/gxp_writer.hpp"
#include <fstream>
#include <iostream>
using namespace vsc;using namespace vsc::backend;
int main(int argc,char**argv){if(argc!=3)return 2;bool premult=argv[2][0]=='1';MachineProgram p;
auto pa=p.physical(machine_primary(0),MachineType::F32);
auto sa=[&](int n){return p.physical(machine_secondary(n*2),MachineType::F32);};
auto reg=[&](int n,int component=255){return p.physical(usse::RegisterBank::Temp,n,MachineType::F32,component);};
auto op=[&](usse::VectorOp operation,MachineOperand dst,MachineOperand a,MachineOperand b){return p.emit_config<MachineOpcode::Vector>((uint8_t)operation,machine_vector_config(15),dst,a,b);};
p.emit<MachineOpcode::Phase>();p.emit<MachineOpcode::Nop>();
op(usse::VectorOp::Mul,reg(4),pa,sa(0));
for(int c=0;c<3;c++)op(usse::VectorOp::Mul,reg(6+c*2),reg(4,c),sa(c+1));
op(usse::VectorOp::Add,reg(12),reg(6),reg(8));op(usse::VectorOp::Add,reg(12),reg(12),reg(10));
if(premult){op(usse::VectorOp::Mul,reg(6),reg(4,3),sa(4));op(usse::VectorOp::Add,reg(12),reg(12),reg(6));}
else op(usse::VectorOp::Add,reg(12),reg(12),sa(4));
op(usse::VectorOp::Mul,reg(6),reg(4,3),sa(5));op(usse::VectorOp::Add,reg(12),reg(12),reg(6));
p.emit_config<MachineOpcode::PackValue>(machine_pack_subop(usse::PackFormat::F32,usse::PackFormat::F16),machine_pack_config(15,true,false),p.physical(machine_fragment_output(0),MachineType::F16),reg(12));
MachineCompileResult compiled;if(!compile_machine_program(p,compiled)){std::cerr<<compiled.error<<'\n';return 3;}
uint8_t iface[32]={};iface[10]=1;iface[11]=4;iface[12]=1;iface[14]=1;iface[16]=4;iface[21]=0xf9;iface[28]=0xc0;uint8_t ext[8]={0x30};uint16_t query[16]={0x0301};
gxp::ParameterContainerDesc container={14,0,0,24};gxp::ParameterDesc parameters[]={
{"uTintColor",1,0,4,14,0,0,1,0},{"uColumn0",1,0,4,14,0,0,1,4},{"uColumn1",1,0,4,14,0,0,1,8},{"uColumn2",1,0,4,14,0,0,1,12},{"uOffset",1,0,4,14,0,0,1,16},{"uAlpha",1,0,4,14,0,0,1,20},{"tex",2,0,4,0,1,0,1,0}};
gxp::ProgramImage image{};image.type=gxp::ProgramType::Fragment;image.minor_version=5;image.sdk_version=0x300;image.program_flags=0x00180804;image.buffer_flags=0x10000000;image.texunit_flags[0]=1;image.primary_register_count=4;image.secondary_register_count=24;image.temp_register_count=32;image.primary_phase_count=1;image.default_uniform_buffer_count=24;image.compiler_version_raw=0x33a90;image.interface_block=iface;image.interface_block_size=sizeof(iface);image.fragment_interface_extension=ext;image.fragment_interface_extension_size=sizeof(ext);image.sampler_query_info=query;image.sampler_query_info_count=16;image.containers=&container;image.container_count=1;image.parameters=parameters;image.parameter_count=7;image.primary_instructions=compiled.words.data();image.primary_instruction_count=compiled.words.size();auto size=gxp::required_size(image);std::vector<uint8_t> bytes(size);if(!size||!gxp::write_program(image,bytes.data(),size))return 4;std::ofstream out(argv[1],std::ios::binary);out.write((char*)bytes.data(),bytes.size());std::cout<<bytes.size()<<" bytes, "<<compiled.words.size()<<" instructions\n";
}
