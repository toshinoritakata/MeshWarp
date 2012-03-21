#version 110
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect diffuseMap;

void main()
{
	gl_FragColor.rgb = texture2DRect( diffuseMap, gl_TexCoord[0].st ).rgb;
	gl_FragColor.a = 1.0;
}