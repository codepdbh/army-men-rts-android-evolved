#version 450
layout(location=0) in vec4 position;
layout(location=1) in vec4 color;
layout(location=2) in vec2 uv0;
layout(location=3) in vec2 uv1;
layout(location=0) out vec4 diffuse;
layout(location=1) out vec2 tex0;
layout(location=2) out vec2 tex1;
void main(){gl_Position=position;diffuse=color;tex0=uv0;tex1=uv1;}
