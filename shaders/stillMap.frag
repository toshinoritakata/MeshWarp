#version 110

uniform sampler2D diffuseMap;

void main()
{
	gl_FragColor.rgb = texture2D(diffuseMap, gl_TexCoord[0].st).rgb;
}