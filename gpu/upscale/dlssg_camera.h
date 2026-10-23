// SPDX-License-Identifier: GPL-2.0-or-later
// Unjittered positive-view-Z, depth [0,1], row-vector camera constants.
#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace Vulkan::DlssgCamera {
using Matrix=std::array<double,16>;
inline Matrix Identity(){Matrix m{};for(unsigned k=0;k<4;++k)m[k*5]=1;return m;}
inline Matrix Multiply(const Matrix& a,const Matrix& b){Matrix c{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)c[i*4+j]+=a[i*4+k]*b[k*4+j];return c;}
inline std::array<float,16> Packed(const Matrix& m){std::array<float,16> result;for(unsigned k=0;k<16;++k){if(!std::isfinite(m[k]))throw std::runtime_error("finite DLSSG matrix required");result[k]=float(m[k]);}return result;}
struct Camera {Matrix vp,inverse_vp;nlohmann::json constants;};
template<class Scene> Camera Prepare(const Scene& in,const Camera* previous,bool reset){
    using J=nlohmann::json;
    const double near=in.camera_near,far=in.camera_far,fov=in.vertical_fov;
    if(!(near>0&&far>near&&fov>0&&fov<3.141593))throw std::runtime_error("valid DLSSG perspective required");
    const float* axes[]={in.camera_right,in.camera_up,in.camera_forward};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){double dot=0;for(unsigned k=0;k<3;++k)dot+=double(axes[i][k])*axes[j][k];if(!std::isfinite(dot)||std::abs(dot-(i==j?1:0))>1e-3)throw std::runtime_error("orthonormal DLSSG camera required");}
    Matrix view=Identity(),inverse_view=Identity();
    for(unsigned i=0;i<3;++i){view[12+i]=0;for(unsigned j=0;j<3;++j){view[j*4+i]=axes[i][j];inverse_view[i*4+j]=axes[i][j];view[12+i]-=double(in.camera_position[j])*axes[i][j];}inverse_view[12+i]=in.camera_position[i];}
    const double q=1/std::tan(fov/2),a=q/(double(in.width)/in.height),c=far/(far-near),b=-near*c;
    Matrix projection{},inverse_projection{};projection[0]=a;projection[5]=q;projection[10]=c;projection[11]=1;projection[14]=b;
    inverse_projection[0]=1/a;inverse_projection[5]=1/q;inverse_projection[11]=1/b;inverse_projection[14]=1;inverse_projection[15]=-c/b;
    Camera out{Multiply(view,projection),Multiply(inverse_projection,inverse_view),{}};
    const auto to_previous=previous&&!reset?Multiply(out.inverse_vp,previous->vp):Identity();
    const auto from_previous=previous&&!reset?Multiply(previous->inverse_vp,out.vp):Identity();
    const auto vec=[](const float(&v)[3]){return J::array({v[0],v[1],v[2]});};
    out.constants={{"camera_view_to_clip",Packed(projection)},{"clip_to_camera_view",Packed(inverse_projection)},
        {"clip_to_previous_clip",Packed(to_previous)},{"previous_clip_to_clip",Packed(from_previous)},
        {"camera_position",vec(in.camera_position)},{"camera_up",vec(in.camera_up)},
        {"camera_right",vec(in.camera_right)},{"camera_forward",vec(in.camera_forward)},
        {"near",near},{"far",far},{"vertical_fov",fov},{"jitter",{in.jitter_x,in.jitter_y}}};
    return out;
}
} // namespace Vulkan::DlssgCamera
