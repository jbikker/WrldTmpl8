#pragma once

namespace Tmpl8 {

struct aabb
{
	float3 bmin, bmax;
};

class Texture
{
public:
	Texture() : m_B32( 0 ) {}
	void Init( char* a_File );
	const unsigned int* GetBitmap() const { return m_B32; }
	const unsigned int GetWidth() const { return m_Width; }
	const unsigned int GetHeight() const { return m_Height; }
	// data members
	unsigned int* m_B32;			
	unsigned int m_Width, m_Height;	
};

class Material
{
public:
	Material();
	~Material();
	void SetDiffuse( const float3& a_Diff );
	const float3 GetDiffuse() const { return m_Diff; }
	void SetTexture( const Texture* a_Texture ) { m_Texture = (Texture*)a_Texture; }
	const Texture* GetTexture() const { return m_Texture; }
	void SetName( char* a_Name );
	char* GetName() { return m_Name; }
	void SetIdx( unsigned int a_Idx ) { m_Idx = a_Idx; }
	unsigned int GetIdx() { return m_Idx; }
private:
	Texture* m_Texture;
	unsigned int m_Idx;
	float3 m_Diff;
	char* m_Name;
};

class MatManager
{
public:
	MatManager();
	void LoadMTL( char* a_File );
	Material* FindMaterial( char* a_Name );
	Material* GetMaterial( int a_Idx ) { return m_Mat[a_Idx]; }
	void Reset() { m_NrMat = 0; }
	void AddMaterial( Material* a_Mat ) { m_Mat[m_NrMat++] = a_Mat; }
	unsigned int GetMatCount() { return m_NrMat; }
private:
	Material** m_Mat;			
	unsigned int m_NrMat;		
};

class Vertex
{
public:
	Vertex() {};
	Vertex( float3 a_Pos ) { m_Pos = a_Pos; }
	const float3& GetNormal() const { return m_N; }
	const float3& GetPos() const { return m_Pos; }
	void SetPos( const float3& a_Pos ) { m_Pos = a_Pos; }
	void SetNormal( const float3& a_N ) { m_N = a_N; }
	const float GetU() const { return m_U; }
	const float GetV() const { return m_V; }
	void SetUV( float a_U, float a_V ) { m_U = a_U; m_V = a_V; }
	// member data
	float3 m_N, m_NO;		
	float3 m_Pos, m_Orig;		
	float m_U, m_V;		
};

class Primitive
{
public:
	Primitive() {};
	void Init( Vertex* a_V1, Vertex* a_V2, Vertex* a_V3 );
	const Material* GetMaterial() const { return m_Material; }
	void SetMaterial( const Material* a_Mat ) { m_Material = (Material*)a_Mat; }
	// triangle primitive methods
	const float3 GetNormal() const { return m_N; }
	void SetNormal( const float3& a_N ) { m_N = a_N; }
	void UpdateNormal();
	const Vertex* GetVertex( const uint a_Idx ) const { return m_Vertex[a_Idx]; }
	void SetVertex( const uint a_Idx, Vertex* a_Vertex ) { m_Vertex[a_Idx] = a_Vertex; }
	const float3 GetNormal( float u, float v ) const { return m_N; }
	// data members
	float3 m_N, m_NO;				
	Vertex* m_Vertex[3];		
	Material* m_Material;		
};

class Scene
{
public:
	Scene();
	void InitSceneState();
	bool InitScene( const char* a_File );
	const aabb& GetExtends() { return m_Extends; }
	void SetExtends( aabb a_Box ) { m_Extends = a_Box; }
	MatManager* GetMatManager() { return m_MatMan; }
	void UpdateSceneExtends();
	void LoadOBJ( const char* filename );
	uint m_Primitives, m_MaxPrims;
	aabb m_Extends;
	MatManager* m_MatMan;
	Primitive* m_Prim;
	char* path, *file, *noext;
};

}

// EOF