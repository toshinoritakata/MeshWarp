#version 110

attribute vec3 tangent;
varying vec3 lightVec;
varying vec3 eyeVec;

uniform vec3 lightLoc;
	
void main()
{
	gl_TexCoord[0] =  gl_MultiTexCoord0;	  
	gl_Position = ftransform();
}
