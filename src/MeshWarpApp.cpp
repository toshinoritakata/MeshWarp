//
// 2012 Codelight inc
//
// Projection Mapping Adjust tool.
//
#include "cinder/app/AppBasic.h"
#include "cinder/gl/Texture.h"
#include "cinder/gl/GlslProg.h"
#include "cinder/Camera.h"
#include "cinder/ImageIo.h"
#include "cinder/qtime/QuickTime.h"
#include "cinder/Utilities.h"
#include "cinder/Text.h"
#include "cinder/params/Params.h"
#include "cinder/TriMesh.h"
#include "cinder/Xml.h"
#include "cinder/gl/Texture.h"
#include "cinder/gl/Fbo.h"
#include "cinder/gl/TextureFont.h"
#include "boost/format.hpp"
#include <windows.h>
#include "Shlwapi.h"

#include "Resources.h"

using namespace ci;
using namespace ci::app;

struct CtrlPoint {
	Vec2f pos;
	Vec2f base;
	Vec2f mag;
	bool  isSelected;
};

// 大変形用のメッシュ
GLfloat distpoints[2][2][2];
GLfloat texpts[2][2][2] = {{{0.0, 0.0},		{1919.0, 0.0}},
							{{0.0, 1079.0},	{1919.0, 1079.0}}};

//const char SeqImageDir[] = "E:\\Work\\ProjectionMap";
const char SeqImageDir[] = "C:";


Vec2f bezier1d(const Vec2f p[4], const float t)
{
	float tn = (1.-t);

	float b0 = tn*tn*tn;
	float b1 = 3.0*t*tn*tn;
	float b2 = 3.0*t*t*tn;
	float b3 = t*t*t;

	Vec2f r;
	r.x = p[0].x*b0 + p[1].x*b1 + p[2].x*b2 + p[3].x*b3;
	r.y = p[0].y*b0 + p[1].y*b1 + p[2].y*b2 + p[3].y*b3;
	
	return r;
}

Vec2f bezierNrm(const Vec2f p[4], const float t)
{
	float len = 0;
	const int div = 8;
	float seg[16];
	float tl[16];
	Vec2f pp[16];

	// 曲線の点を計算
	for (int n = 0; n < div; ++n) {
		float dt = (float)n/(div-1);
		pp[n] = bezier1d(p, dt);
		tl[n] = dt;
	}

	// 曲線の長さを出す
	seg[0] = 0.0;
	for (int n = 0; n < div-1; ++n) {
		float d = pp[n].distance(pp[n+1]);
		len += d;
		seg[n+1] = len;
	}

	// 正規化して距離をパラメータとして扱えるようにする
	for (int n = 1; n < div; ++n) {
		seg[n] = seg[n]/len;
	}

	float tt = 0.0;
	for (int n = 0; n < div-1; ++n) {
		if (t >= seg[n] && t <= seg[n+1]) {
			float dt = (t-seg[n]) / (seg[n+1]-seg[n]);
			tt = (tl[n]*(1.0-dt)) + (tl[n+1]*dt);
			break;
		}
	}
	
	return bezier1d(p, tt);
}

enum DispMode {
	DispMode_GUIDE = 0,
	DispMode_GUIDE1,
	DispMode_GUIDE2,
	DispMode_GUIDE3,
	DispMode_GUIDE4,
	DispMode_MOVIE
};
enum EditMode {
	EditMode_EDIT = 0,
	EditMode_RECORD
};
class ProjectionMappingApp : public AppBasic {
private:
	params::InterfaceGl		mParams;
	float					mHandleSize;
	TriMesh					mMesh;
	Vec2i					mGridNum;
	bool					mSelectionMode;
	Rectf					mSelectRegion;
	CtrlPoint*				mCtrlPoints;
	int 					mCtrlPointsNum;
	Vec2f					mMPPrev;
	float					mScale;
	gl::Texture				mSequenceImage;
	gl::Fbo					mFbo;
	bool					mIsShowCtrlMesh;
	gl::TextureFontRef		mTexFont;
	int						mSpan;
	enum DispMode			mDispMode;
	bool					mMeshMode;
	enum EditMode			mEditMode;
	int						mFrame;
	int						mDuration;
	bool					mHasMovie;
	std::string				mOutImageName;
	std::string				mOutImageFolder;
	std::string				mInImageName;
	std::string				mInImageFolder;

public:
	ProjectionMappingApp() 
		: mDuration(4000)
		, mOutImageName("Ref_comp1")
		, mOutImageFolder("C:\\out_images") 
		, mInImageName("Ref_comp1")
		, mInImageFolder("C:\\in_images") 
	{}

	void prepareSettings( Settings *settings );
	void setup();

	void resetMesh();
	
	void mouseDown( MouseEvent event )
	{
		if (event.isLeftDown()) {
			mSelectionMode = 1; // 選択開始
			mSelectRegion.set(event.getX()-5, event.getY()-5, event.getX()+5, event.getY()+5);
		}

		mMPPrev = event.getPos();
	}

	void mouseUp( MouseEvent event )
	{
		mMPPrev = Vec2f::zero();
		mSelectionMode = 0; // 選択終了
	}
	
	void mouseDrag( MouseEvent event ) 
	{
		if (mEditMode == EditMode_RECORD) return;

		// 制御点の選択を行う
		if (event.isLeftDown()) {
			if (mSelectionMode == 0) return;

			float x1 = mSelectRegion.getX1();
			float y1 = mSelectRegion.getY1();
			mSelectRegion.set(x1, y1, event.getX()+5, event.getY()+5);

			for (int k = 0; k < mCtrlPointsNum; ++k) {
				if (mSelectRegion.contains(mCtrlPoints[k].pos)) {
					mCtrlPoints[k].isSelected = true;
				} else {
					mCtrlPoints[k].isSelected = false;
				}
			}
		}

		Vec2f d = event.getPos() - mMPPrev;	

		if (event.isRightDown()) {
			for (int k = 0; k < mCtrlPointsNum; ++k) {
				if (mCtrlPoints[k].isSelected) {
					mCtrlPoints[k].mag += d;
				}
			}
		}

		if (event.isMiddleDown()) {
			int ci = -1;
			int cj = -1;
			float nearest = 1.e+10;
			for (int j = 0; j < 2; ++j) {	
				for (int i = 0; i < 2; ++i) {
					Vec2f cp(distpoints[j][i][0], distpoints[j][i][1]);
					float dist = event.getPos().distance(cp);
					if (dist < nearest) {
						ci = i;
						cj = j;
						nearest = dist;
					}
				}
			}

			if (ci > -1 &&  cj > -1) {
				distpoints[cj][ci][0] += d.x;
				distpoints[cj][ci][1] += d.y;
			}
		}
		mMPPrev = event.getPos();
	}

	void keyDown(KeyEvent event)
	{
		if (mEditMode == EditMode_RECORD) return;

		if (event.getCode() == KeyEvent::KEY_ESCAPE) {
			delete [] mCtrlPoints;
			exit(1);
		}

		if (event.getChar() == 'p') {
			if (mParams.isVisible()) {
				mParams.hide();
			} else { 
				mParams.show();
			}
		}

		if (event.getCode() == KeyEvent::KEY_F1) { mScale = 0.5;	}
		if (event.getCode() == KeyEvent::KEY_F2) { mMeshMode  = (!mMeshMode); }
		if (event.getCode() == KeyEvent::KEY_F12) { resetMesh(); }
		
		if (event.getCode() == KeyEvent::KEY_F5) { 
			if (mHasMovie) mMovie.stop();
			mDispMode = DispMode_GUIDE; 
		}
		if (event.getCode() == KeyEvent::KEY_F6) { 
			if (mHasMovie) mMovie.stop();
			mDispMode = DispMode_GUIDE1; 
		}
		if (event.getCode() == KeyEvent::KEY_F7) { 
			if (mHasMovie) mMovie.stop();
			mDispMode = DispMode_GUIDE2; 
		}
		if (event.getCode() == KeyEvent::KEY_F8) { 
			if (mHasMovie) mMovie.stop();
			mDispMode = DispMode_GUIDE3; 
		}
		if (event.getCode() == KeyEvent::KEY_F9) { 
			if (mHasMovie) mMovie.stop();
			mDispMode = DispMode_GUIDE4; 
		}
		if (event.getCode() == KeyEvent::KEY_F10) { 
			if (mHasMovie) {
				mMovie.play();
				mDispMode = DispMode_MOVIE; 
			}
		}

/*
		if (event.getCode() == KeyEvent::KEY_SPACE)	{
			if (mDispMode == DispMode_GUIDE) {
				mDispMode = DispMode_MOVIE;
			} 
			else if (mDispMode == DispMode_MOVIE) {
				mDispMode = DispMode_GUIDE;
			}
		}
		*/

		Vec2f move = Vec2f::zero();
		if (event.getCode() == KeyEvent::KEY_UP)	{ move = Vec2f( 0, -1); }
		if (event.getCode() == KeyEvent::KEY_DOWN)	{ move = Vec2f( 0,  1); }
		if (event.getCode() == KeyEvent::KEY_LEFT)	{ move = Vec2f(-1,  0); }
		if (event.getCode() == KeyEvent::KEY_RIGHT) { move = Vec2f( 1,  0); }
		for (int k = 0; k < mCtrlPointsNum; ++k) {
			if (mCtrlPoints[k].isSelected) {
				mCtrlPoints[k].mag += move;
			}
		}
	}

	void keyUp( KeyEvent event )
	{
		if (event.getChar() == 'd') {
			mIsShowCtrlMesh = (!mIsShowCtrlMesh);
		}

		if (event.getCode() == KeyEvent::KEY_F1) { mScale = 1.0; }
	}

	
	void draw();
	void update();
	
	void onSelectInFolder()
	{
		mInImageFolder = getFolderPath(mInImageFolder);
	}

	void onSelectOutFolder()
	{
		mOutImageFolder = getFolderPath(mOutImageFolder);
	}

	void onRecode() 
	{
		mFrame = 0;
	//	mOutImageFolder = getFolderPath(mOutImageFolder);

		mEditMode = EditMode_RECORD;
	}
	
	void onReadMovie()
	{
		std::string moivePath = getOpenFilePath();
		if (!moivePath.empty()) {
			loadMovieFile(moivePath);
		}
	}

	void onWrite() 
	{
		char buf[256];

		XmlTree pos("pos", "");

		XmlTree frame("frame", "");
		frame.push_back(XmlTree("frame", (boost::format("%f %f") % distpoints[0][0][0] % distpoints[0][0][1]).str()));
		frame.push_back(XmlTree("frame", (boost::format("%f %f") % distpoints[0][1][0] % distpoints[0][1][1]).str()));
		frame.push_back(XmlTree("frame", (boost::format("%f %f") % distpoints[1][0][0] % distpoints[1][0][1]).str()));
		frame.push_back(XmlTree("frame", (boost::format("%f %f") % distpoints[1][1][0] % distpoints[1][1][1]).str()));

		XmlTree mag("mag", "");
		for (int k = 0; k < mCtrlPointsNum; ++k) {
			sprintf(buf, "%f %f", mCtrlPoints[k].mag.x, mCtrlPoints[k].mag.y);
			mag.push_back(XmlTree("mag", buf));
		}

		pos.push_back(frame);
		pos.push_back(mag);

		pos.write(writeFile(getHomeDirectory()+std::string("\\temp.xml")));
	}

	void onRead()
	{
		XmlTree doc(loadFile(getHomeDirectory()+std::string("\\temp.xml")));

		Vec2f pp[4];
		int n = 0;
		for (XmlTree::Iter it = doc.begin("pos/frame/frame"); it != doc.end(); ++it) {
			app::console() << it->getValue() << std::endl;
			sscanf(it->getValue().c_str(), "%f %f", &pp[n].x, &pp[n].y);
			++n;
		}
		distpoints[0][0][0] = pp[0].x;
		distpoints[0][0][1] = pp[0].y;
		distpoints[0][1][0] = pp[1].x;
		distpoints[0][1][1] = pp[1].y;
		distpoints[1][0][0] = pp[2].x;
		distpoints[1][0][1] = pp[2].y;
		distpoints[1][1][0] = pp[3].x;
		distpoints[1][1][1] = pp[3].y;
		
		n = 0;
		for (XmlTree::Iter it = doc.begin("pos/mag/mag"); it != doc.end(); ++it) {
			sscanf(it->getValue().c_str(), "%f %f", &mCtrlPoints[n].mag.x, &mCtrlPoints[n].mag.y);
			++n;
		}
	}

	Vec2f deform(const Vec2f p);
	void bezierMesh(const int mode);
	void createBezierMesh() { this->bezierMesh(0);}
	void updateBezierMesh() { this->bezierMesh(1);}
	void loadMovieFile( const std::string &moviePath )
	{
		glEnable(GL_TEXTURE_RECTANGLE_ARB);
		gl::Texture::Format format;
		format.setTargetRect();

		try {
			// load up the movie, set it to loop, and begin playing
			mMovie = qtime::MovieGl( moviePath );
			mMovie.setLoop();
//			mMovie.play();

			// create a texture for showing some info about the movie
			TextLayout infoText;
			infoText.clear( ColorA( 0.2f, 0.2f, 0.2f, 0.5f ) );
			infoText.setColor( Color::white() );
			infoText.addCenteredLine( getPathFileName( moviePath ) );
			infoText.addLine( toString( mMovie.getWidth() ) + " x " + toString( mMovie.getHeight() ) + " pixels" );
			infoText.addLine( toString( mMovie.getDuration() ) + " seconds" );
			infoText.addLine( toString( mMovie.getNumFrames() ) + " frames" );
			infoText.addLine( toString( mMovie.getFramerate() ) + " fps" );
			infoText.setBorder( 4, 2 );
			mInfoTexture = gl::Texture( infoText.render( true ) );
			
			setWindowSize(mMovie.getWidth(), mMovie.getHeight());
			mHasMovie = true;
		}
		catch( ... ) {
			console() << "Unable to load the movie." << std::endl;
			mMovie.reset();
			mInfoTexture.reset();
			mHasMovie = false;
			//exit(1);
		}
		mFrameTexture.reset();
	}
	
	gl::Texture		mFrameTexture, mInfoTexture;
	qtime::MovieGl	mMovie;
	gl::GlslProg	mShader;
	gl::Texture		mDiffuseTex[5];
};

void ProjectionMappingApp::prepareSettings( Settings *settings )
{
	settings->setFrameRate(30.0);
}

void ProjectionMappingApp::bezierMesh(const int mode)
{

	const int span = mSpan;
	float hx = mHandleSize;
	float hy = mHandleSize;

	//if (mode == 0) { mMesh.clear();	}

	int k = 0;
	for (int ix = 0; ix < mGridNum.x-1; ++ix) {
		for (int iu = 0; iu < span; ++iu) {
			if (ix > 0 && iu == 0) continue;

			for (int iy = 0; iy < mGridNum.y-1; ++iy) {
				for (int iv = 0; iv < span; ++iv) {
					if (iy > 0 && iv == 0) continue;

					int loc0 = ((ix+0)*(mGridNum.y)) + (iy+0);
					int loc1 = ((ix+0)*(mGridNum.y)) + (iy+1);
					int loc2 = ((ix+1)*(mGridNum.y)) + (iy+0);
					int loc3 = ((ix+1)*(mGridNum.y)) + (iy+1);

					Vec2f p[4][4];
					p[0][0] = deform(mCtrlPoints[loc0].base+Vec2f(0,  0))  + mCtrlPoints[loc0].mag;
					p[0][1] = deform(mCtrlPoints[loc0].base+Vec2f(0,  hy)) + mCtrlPoints[loc0].mag;
					p[0][2] = deform(mCtrlPoints[loc1].base+Vec2f(0, -hy)) + mCtrlPoints[loc1].mag;
					p[0][3] = deform(mCtrlPoints[loc1].base+Vec2f(0,  0))  + mCtrlPoints[loc1].mag;

					p[1][0] = deform(mCtrlPoints[loc0].base+Vec2f(hx,  0))  + mCtrlPoints[loc0].mag;
					p[1][1] = deform(mCtrlPoints[loc0].base+Vec2f(hx,  hy)) + mCtrlPoints[loc0].mag;
					p[1][2] = deform(mCtrlPoints[loc1].base+Vec2f(hx, -hy)) + mCtrlPoints[loc1].mag;
					p[1][3] = deform(mCtrlPoints[loc1].base+Vec2f(hx,  0))  + mCtrlPoints[loc1].mag;

					p[2][0] = deform(mCtrlPoints[loc2].base+Vec2f(-hx,  0))  + mCtrlPoints[loc2].mag;
					p[2][1] = deform(mCtrlPoints[loc2].base+Vec2f(-hx,  hy)) + mCtrlPoints[loc2].mag;
					p[2][2] = deform(mCtrlPoints[loc3].base+Vec2f(-hx, -hy)) + mCtrlPoints[loc3].mag;
					p[2][3] = deform(mCtrlPoints[loc3].base+Vec2f(-hx,  0))  + mCtrlPoints[loc3].mag;

					p[3][0] = deform(mCtrlPoints[loc2].base+Vec2f(0,  0))  + mCtrlPoints[loc2].mag;
					p[3][1] = deform(mCtrlPoints[loc2].base+Vec2f(0,  hy)) + mCtrlPoints[loc2].mag;
					p[3][2] = deform(mCtrlPoints[loc3].base+Vec2f(0, -hy)) + mCtrlPoints[loc3].mag;
					p[3][3] = deform(mCtrlPoints[loc3].base+Vec2f(0,  0))  + mCtrlPoints[loc3].mag;

					mCtrlPoints[loc0].pos = p[0][0];
					mCtrlPoints[loc1].pos = p[0][3];
					mCtrlPoints[loc2].pos = p[3][0];
					mCtrlPoints[loc3].pos = p[3][3];

					float v = (float)iv/(float)(span-1);
					float u = (float)iu/(float)(span-1);

					Vec2f r[4];
					r[0] = bezierNrm(p[0], v);
					r[1] = bezierNrm(p[1], v);
					r[2] = bezierNrm(p[2], v);
					r[3] = bezierNrm(p[3], v);

					Vec2f fp = bezierNrm(r, u);

					if (mode == 0) { // create
						mMesh.appendVertex(Vec3f(fp.x, fp.y, 0));
						mMesh.appendTexCoord(Vec2f(fp.x, fp.y));
					} else { // update
						mMesh.getVertices()[k] = Vec3f(fp.x, fp.y, 0);
					}
					k++;
				}
			}
		}
	}

	if (mode == 0) { // create
		int nx = ((mGridNum.x-1)*span) - (mGridNum.x-2);
		int ny = ((mGridNum.y-1)*span) - (mGridNum.y-2);

		int id = 0;
		for (int ix = 0; ix < nx-1; ++ix) {
			for (int iy = 0; iy < ny-1; ++iy) {
				int id0 = id;
				int id1 = id+1;
				int id2 = id1+ny;
				int id3 = id2-1;
				mMesh.appendTriangle(id0, id1, id2);
				mMesh.appendTriangle(id0, id2, id3);			
				++id;
			}
			++id;
		}
	}
}

void ProjectionMappingApp::resetMesh()
{
	for (int k = 0; k < mCtrlPointsNum; ++k) {
		mCtrlPoints[k].mag = Vec2f(0.0, 0.0);
		mCtrlPoints[k].isSelected = false;
	}

	distpoints[0][0][0] = 0;
	distpoints[0][0][1] = 0;
	distpoints[0][1][0] = 1920-1;
	distpoints[0][1][1] = 0;
	distpoints[1][0][0] = 0;
	distpoints[1][0][1] = 1080-1;
	distpoints[1][1][0] = 1920-1;
	distpoints[1][1][1] = 1080-1;
}

void ProjectionMappingApp::setup()
{
	mDispMode = DispMode_GUIDE;
	mEditMode = EditMode_EDIT;
	mIsShowCtrlMesh = true;
	mMeshMode = false;
	mHandleSize = 30.0;
	mGridNum = Vec2i(16, 8);
	mMPPrev = Vec2f(0, 0);
	mSpan = 8;
	mHasMovie = false;

	glEnable(GL_TEXTURE_RECTANGLE_ARB);

	mTexFont = gl::TextureFont::create(Font("msgothic", 40));

	gl::Fbo::Format format;
	format.enableDepthBuffer(false);
	format.enableMipmapping(false);
	mFbo = gl::Fbo(1920, 1080, format);

	mScale = 1.0f;
	mParams = params::InterfaceGl( "App parameters", Vec2i( 200, 400 ) );

	// In Image
	mParams.addButton("SelectInImageFolder", std::bind(&ProjectionMappingApp::onSelectInFolder, this));
	mParams.addParam("mInImageFolder ", &mInImageFolder, mInImageFolder );
	mParams.addParam("InImageName ", &mInImageName, mInImageName );

	// Out Image
	mParams.addButton("SelectOutImageFolder", std::bind(&ProjectionMappingApp::onSelectOutFolder, this));
	mParams.addParam("mOutImageFolder ", &mOutImageFolder, mOutImageFolder );
	mParams.addParam("OutImageName ", &mOutImageName, mOutImageName );
	mParams.addParam("FrameDuration", &mDuration);
//	mOutImageFolder = getFolderPath(mOutImageFolder);

	mParams.addSeparator();

	mParams.addButton("Recode", std::bind(&ProjectionMappingApp::onRecode, this));

	mParams.addSeparator();
	mParams.addButton("WriteXML", std::bind(&ProjectionMappingApp::onWrite, this));
	mParams.addButton("ReadXML", std::bind(&ProjectionMappingApp::onRead, this));
	mParams.addSeparator();

	glDisable(GL_DEPTH);
	glDisable(GL_DEPTH_TEST);

	mShader = gl::GlslProg( loadResource( RES_NORMAL_MAP_VERT ), loadResource( RES_NORMAL_MAP_FRAG ) );
	
	gl::Texture::Format texformat;
	texformat.setTargetRect(); // GL_TEXTURE_RECTANGLE_ARBとして使う

	try
	{
		mDiffuseTex[DispMode_GUIDE]  = gl::Texture(loadImage("resources/Guide.jpg"), texformat);
		mDiffuseTex[DispMode_GUIDE1] = gl::Texture(loadImage("resources/0_CRV_mask.jpg"), texformat);
		mDiffuseTex[DispMode_GUIDE2] = gl::Texture(loadImage("resources/0_CRV_Line.jpg"), texformat);
		mDiffuseTex[DispMode_GUIDE3] = gl::Texture(loadImage("resources/0_CRV_LineGuide.jpg"), texformat);
		mDiffuseTex[DispMode_GUIDE4] = gl::Texture(loadImage("resources/0_CRV_TireGuide.jpg"), texformat);
	}
	catch (cinder::ImageIoExceptionFailedLoad * e)
	{
		app::console() << e->what() << std::endl;
		exit(1);
	}

	std::string moivePath = getOpenFilePath();
	if (!moivePath.empty()) {
		loadMovieFile(moivePath);
	}

	Vec2f result(1920-1, 1080-1);
	Vec2f div(mGridNum);

	int nx = ((mGridNum.x-1)*mSpan) - (mGridNum.x-2);
	int ny = ((mGridNum.y-1)*mSpan) - (mGridNum.y-2);
	
	mCtrlPointsNum = nx*ny;
	mCtrlPoints = new CtrlPoint[mCtrlPointsNum];

	int k = 0;
	for (int ix = 0; ix < mGridNum.x; ++ix) {
		for (int iy = 0; iy < mGridNum.y; ++iy) {
			float x = (result.x/(div.x-1)) * static_cast<float>(ix);
			float y = (result.y/(div.y-1)) * static_cast<float>(iy);

			CtrlPoint cp;
			cp.pos.set(x, y);
			cp.base.set(x, y);
			cp.mag.set(0, 0);
			cp.isSelected = false;
			mCtrlPoints[k] = cp;
			++k;
		}
	}

	resetMesh();

	// 頂点のバッファを確保
	this->createBezierMesh();

	setFullScreen(true);
}

Vec2f ProjectionMappingApp::deform(const Vec2f p)
{
	Vec2f p0(distpoints[0][0][0], distpoints[0][0][1]);
	Vec2f p1(distpoints[0][1][0], distpoints[0][1][1]);
	Vec2f p2(distpoints[1][0][0], distpoints[1][0][1]);
	Vec2f p3(distpoints[1][1][0], distpoints[1][1][1]);

	float rx = p.x/(1920-1);
	float ry = p.y/(1080-1);

	Vec2f pp0 = p0.lerp(rx, p1);
	Vec2f pp1 = p2.lerp(rx, p3);

	return pp0.lerp(ry, pp1);
}

void ProjectionMappingApp::update()
{
	this->updateBezierMesh();// update

	gl::setMatricesWindow(getWindowSize(), false); // 画像の表示を上向きにする
	mFbo.bindFramebuffer();
	gl::clear(Color(0, 0, 0));

	mShader.bind();
	mShader.uniform("diffuseMap", 0);

#if 0
	mMovie.getTexture().bind(0);
#else 
	if (mEditMode == EditMode_EDIT) {
		if (mDispMode == DispMode_MOVIE && mHasMovie) {
			mMovie.getTexture().bind(0);
		} else {
			mDiffuseTex[mDispMode].bind(0);
		}
	} else if (mEditMode == EditMode_RECORD) {
		if (mFrame <= mDuration) {
			gl::Texture::Format texformat;
			texformat.setTargetRect();

			std::string imgName = (boost::format("%s\\%s_%05d.png") % mInImageFolder % mInImageName % mFrame).str();
			if (boost::filesystem::exists(boost::filesystem::path(imgName))) {
				mDiffuseTex[mDispMode] = gl::Texture(loadImage(imgName.c_str()), texformat);
			}
			else {
				mDispMode = DispMode_GUIDE;
				mEditMode = EditMode_EDIT;
			}
		} else {
			mEditMode = EditMode_EDIT;
		}
	}
#endif

	gl::draw(mMesh);
	mShader.unbind();

	mFbo.unbindFramebuffer();
}

void ProjectionMappingApp::draw()
{
	if (mEditMode == EditMode_RECORD) {
		//		std::string imgName = (boost::format("%s\\out_images\\image_%05d.png") % SeqImageDir % mFrame).str();
		
		std::string imgName = (boost::format("%s\\%s_%05d.png") % mOutImageFolder % mOutImageName % mFrame).str();
			
		writeImage(imgName.c_str(), mFbo.getTexture());
		++mFrame;
	}

	gl::clear(Color(0, 0, 0));
	glColor3f(1, 1, 1);

	// 画像の表示を下向きに(もとに戻す)
	gl::setMatricesWindow(getWindowSize(), true); 
	
	glPushMatrix();
	if (mScale < 1.0) {
		Vec2f wc = getWindowCenter();
		glTranslatef(wc.x/2, wc.y/2, 0.0);
		glScalef(mScale, mScale, mScale);
	}

	if (mMeshMode) {
		glColor3f(1, 1, 0);
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		gl::draw(mMesh);
	} else {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		gl::draw(mFbo.getTexture());
	}

	if (mIsShowCtrlMesh) {
		for (int k = 0; k < mCtrlPointsNum; ++k) {
			Vec2f p = mCtrlPoints[k].pos;
			glColor3f(0, 0, 0);
			gl::drawStrokedRect(Rectf(p.x-5+1, p.y-5+1, p.x+5+1, p.y+5+1));
			if (mCtrlPoints[k].isSelected) {
				glColor3f(1, 0, 0);
			} else {
				glColor3f(1, 1, 1);
			}
			gl::drawStrokedRect(Rectf(p.x-5, p.y-5, p.x+5, p.y+5));
		}

		for (int j = 0; j < 2; ++j) {	
			for (int i = 0; i < 2; ++i) {
				float px = distpoints[j][i][0];
				float py = distpoints[j][i][1];
				glColor3f(1, 1, 0);
				gl::drawStrokedRect(Rectf(px-10, py-10, px+10, py+10));
			}
		}
	}

	glPopMatrix();

	if (mSelectionMode) gl::drawStrokedRect(mSelectRegion);
	params::InterfaceGl::draw();
	
	if (mEditMode == EditMode_RECORD) {
		gl::enableAlphaBlending();
		const int ix = 700;
		const int iy = 900;
		mTexFont->drawString((boost::format("RENDERING : [%d / %d] Done.") % mFrame % mDuration).str(), Rectf(ix, iy, ix+500, iy+200));
	}
}

CINDER_APP_BASIC( ProjectionMappingApp, RendererGl(RendererGl::AA_NONE) )