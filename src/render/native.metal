#include <metal_stdlib>
using namespace metal;

// New native rendering contract, not a recovered Xbox shader. Attribute
// locations and set 0 argument IDs are paired with renderer.cpp's Plume layout.
struct VertexIn {
    float3 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};
struct Raster {
    float4 position [[position]];
    float2 uv;
    float4 color;
};
struct Material {
    texture2d<float> image [[id(0)]];
    sampler image_sampler [[id(1)]];
};
vertex Raster native_vertex(VertexIn in [[stage_in]]) {
    return {float4(in.position, 1.0), in.uv, in.color};
}
fragment float4 native_fragment(Raster in [[stage_in]], constant Material& material [[buffer(0)]]) {
    return material.image.sample(material.image_sampler, in.uv) * in.color;
}
