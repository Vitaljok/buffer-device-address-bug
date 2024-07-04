#version 450
#extension GL_EXT_buffer_reference : require

struct Vertex {
  vec4 position;
  vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer { Vertex vertices[]; };

layout(push_constant) uniform constants { 
  VertexBuffer vertexBuffer; 
} PC;

layout(location = 0) out vec4 outColor;

void main() {
  Vertex vtx = PC.vertexBuffer.vertices[gl_VertexIndex];

  gl_Position = vtx.position;
  outColor = vtx.color;
}