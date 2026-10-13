#version 450
layout(location=0) in vec4 diffuse;
layout(location=1) in vec2 tex0;
layout(location=2) in vec2 tex1;
layout(location=0) out vec4 result;
layout(set=0,binding=0) uniform sampler2D image0;
layout(set=0,binding=1) uniform sampler2D image1;
layout(push_constant) uniform State {uvec4 stage0;uvec4 stage1;uvec4 alpha;vec4 factor;} state;
vec4 argument(uint selector,vec4 textureColor,vec4 current){
    uint source=selector&15u;
    vec4 value=source==0u?diffuse:source==1u?current:source==2u?textureColor:source==3u?state.factor:vec4(1);
    if((selector&32u)!=0u)value=vec4(value.a);
    if((selector&16u)!=0u)value=vec4(1)-value;
    return value;
}
vec4 combine(uvec4 stage,vec4 textureColor,vec4 current){
    vec4 a=argument(stage.y,textureColor,current),b=argument(stage.z,textureColor,current);
    if(stage.x==1u)return current;
    vec4 value=stage.x==2u?a:stage.x==3u?b:stage.x==4u?a*b:stage.x==5u?a*b*2:stage.x==6u?a*b*4:stage.x==7u?a+b:stage.x==10u?a-b:a*b;
    // Separate alpha selectors/operation are packed into the fourth word.
    uint op=stage.w&255u;
    float aa=argument((stage.w>>8)&255u,textureColor,current).a,ab=argument((stage.w>>16)&255u,textureColor,current).a;
    value.a=op==1u?current.a:op==2u?aa:op==3u?ab:aa*ab;
    return clamp(value,0,1);
}
void main(){
    vec4 value=combine(state.stage0,texture(image0,tex0),diffuse);
    value=combine(state.stage1,texture(image1,tex1),value);
    if(state.alpha.x!=0u){float ref=float(state.alpha.z)/255.0;uint fn=state.alpha.y;
        bool pass=fn==1u?false:fn==2u?value.a<ref:fn==3u?value.a==ref:fn==4u?value.a<=ref:fn==5u?value.a>ref:fn==6u?value.a!=ref:fn==7u?value.a>=ref:true;
        if(!pass)discard;
    }
    result=value;
}
