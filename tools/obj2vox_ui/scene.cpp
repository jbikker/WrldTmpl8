#include "precomp.h"

using namespace Tmpl8;

static Scene* scene;

void Texture::Init( char* a_File )
{
	m_B32 = 0;
	FILE* f = fopen( a_File, "rb" );
	if (!f) return;
	FREE_IMAGE_FORMAT fif = FIF_UNKNOWN;
	fif = FreeImage_GetFileType( a_File, 0 );
	if (fif == FIF_UNKNOWN) fif = FreeImage_GetFIFFromFilename( a_File );
	FIBITMAP* tmp = FreeImage_Load( fif, a_File );
	FIBITMAP* dib = FreeImage_ConvertTo32Bits( tmp );
	FreeImage_Unload( tmp );
	unsigned char* bits = FreeImage_GetBits( dib );
	m_Width = FreeImage_GetWidth( dib );
	m_Height = FreeImage_GetHeight( dib );
	m_B32 = (unsigned int*)MALLOC64( m_Width * m_Height * sizeof( unsigned int ) );
	for ( unsigned int y = 0; y < m_Height; y++ ) 
	{
		unsigned char* line = FreeImage_GetScanLine( dib, m_Height - 1 - y );
		memcpy( m_B32 + y * m_Width, line, m_Width * sizeof( unsigned int ) );
	}
	FreeImage_Unload( dib );
}

MatManager::MatManager()
{
	m_Mat = new Material*[1024];
	m_Mat[0] = new Material();
	m_Mat[0]->SetName( "DEFAULT" );
	m_NrMat = 1;
}

void MatManager::LoadMTL( char* a_File )
{
	FILE* f = fopen( a_File, "r" );
	if (!f) return;
	unsigned int curmat = 0;
	char buffer[256], cmd[128];
	while (!feof( f ))
	{
		fgets( buffer, 250, f );
		sscanf( buffer, "%s", cmd );
		if (!_stricmp( cmd, "newmtl" ))
		{
			curmat = m_NrMat++;
			m_Mat[curmat] = new Material();
			char matname[128];
			sscanf( buffer + strlen( cmd ), "%s", matname );
			m_Mat[curmat]->SetName( matname );
		}
		if (!_stricmp( cmd, "Kd" ))
		{
			float r, g, b;
			sscanf( buffer + 3, "%f %f %f", &r, &g, &b );
			float3 c = make_float3( r, g, b );
			m_Mat[curmat]->SetDiffuse( c );
		}
		if (!_stricmp( cmd, "map_Kd" ))
		{
			char tname[128], fname[256];
			tname[0] = fname[0] = 0;
			sscanf( buffer + 7, "%s", tname );
			if (tname[0])
			{
				strcpy( fname, scene->path );
				if (!strstr( tname, "textures/" )) strcat( fname, "textures/" );
				strcat( fname, tname );
				Texture* t = new Texture();
				t->Init( fname );
				if (!t->m_B32)
				{
					strcpy( fname, scene->path );
					strcat( fname, tname );
					t->Init( fname );
				}
				m_Mat[curmat]->SetTexture( t );
			}
		}
	}
}

Material* MatManager::FindMaterial( char* a_Name )
{
	for ( unsigned int i = 0; i < m_NrMat; i++ ) if (!_stricmp( m_Mat[i]->GetName(), a_Name )) return m_Mat[i];
	return m_Mat[0];
}

Material::Material()
{
	m_Texture = 0, m_Name = new char[64];
	SetDiffuse( make_float3( 0.8f, 0.8f, 0.8f ) );
}

Material::~Material()
{
	delete m_Name;
}

void Material::SetDiffuse( const float3& a_Diffuse )
{
	m_Diff = a_Diffuse;
}

void Material::SetName( char* a_Name ) 
{ 
	strcpy( m_Name, a_Name ); 
}

void Primitive::Init( Vertex* a_V1, Vertex* a_V2, Vertex* a_V3 )
{
	m_Vertex[0] = a_V1, m_Vertex[1] = a_V2,	m_Vertex[2] = a_V3;
	UpdateNormal();
}

void Primitive::UpdateNormal()
{
	float3 c = normalize( m_Vertex[1]->GetPos() - m_Vertex[0]->GetPos() );
	float3 b = normalize( m_Vertex[2]->GetPos() - m_Vertex[0]->GetPos() );
	m_N = cross( b, c );
}

Scene::Scene()
{
	m_MatMan = new MatManager();
}

void Scene::LoadOBJ( const char* filename )
{
	FILE* f = fopen( filename, "r" );
	if (!f) return;
	unsigned int fcount = 0, ncount = 0, uvcount = 0, vcount = 0;
	char buffer[256], cmd[256], objname[256];
	while (1)
	{
		fgets( buffer, 250, f );
		if (feof( f )) break;
		if (buffer[0] == 'v')
		{
			if (buffer[1] == ' ') vcount++;
			else if (buffer[1] == 't') uvcount++;
			else if (buffer[1] == 'n') ncount++;
		}
		else if ((buffer[0] == 'f') && (buffer[1] == ' ')) fcount++;
	}
	fclose( f );
	// Node* node = new StaticMesh( fcount );
	if (m_Prim) FREE64( m_Prim ); // a little leaking is fine but this is nuts
	m_Prim = (Primitive*)MALLOC64( sizeof( Primitive ) * fcount );
	// Primitive* prims = (Primitive*)MALLOC64( sizeof( Primitive ) * fcount );
	f = fopen( filename, "r" );
	Material* curmat = 0;
	unsigned int verts = 0, uvs = 0, normals = 0;
	// allocate arrays
	float3* vert = new float3[vcount];
	float3* norm = new float3[ncount];
	float* tu = new float[uvcount];
	float* tv = new float[uvcount];
	Vertex* vertex = (Vertex*)MALLOC64( (fcount * 3 + 4) * sizeof( Vertex ) );
	unsigned int vidx = 0;
	char currobject[256];
	strcpy( currobject, "none" );
	// load data
	m_Primitives = 0;
	while (1)
	{
		fgets( buffer, 250, f );
		if (feof( f )) break;
		switch (buffer[0])
		{
		case 'v':
		{
			if (buffer[1] == ' ')
			{
				float x, y, z;
				sscanf( buffer, "%s %f %f %f", cmd, &x, &y, &z );
				float3 pos = make_float3( x, y, z );
				vert[verts++] = pos;
				for ( unsigned int a = 0; a < 3; a++ )
				{
					if (pos.e[a] < m_Extends.bmin.e[a]) m_Extends.bmin.e[a] = pos.e[a];
					if (pos.e[a] > m_Extends.bmax.e[a]) m_Extends.bmax.e[a] = pos.e[a];
				}
			}
			else if (buffer[1] == 't')
			{
				float u, v;
				sscanf( buffer, "%s %f %f", cmd, &u, &v );
				tu[uvs] = u, tv[uvs++] = -v; // prevent negative uv's
			}
			else if (buffer[1] == 'n')
			{
				float x, y, z;
				sscanf( buffer, "%s %f %f %f", cmd, &x, &y, &z );
				norm[normals++] = make_float3( -x, -y, -z );
			}
		}
		break;
		default:
			break;
		}
	}
	fclose( f );
	f = fopen( filename, "r" );
	while (1)
	{
		fgets( buffer, 250, f );
		if (feof( f )) break;
		switch (buffer[0])
		{
		case 'f':
		{
			// face
			Vertex* v[3];
			float cu[3], cv[3];
			const Texture* t = curmat->GetTexture();
			unsigned int vnr[9];
			unsigned int vars = sscanf( buffer + 2, "%i/%i/%i %i/%i/%i %i/%i/%i", 
				&vnr[0], &vnr[1], &vnr[2], &vnr[3], &vnr[4], &vnr[5], &vnr[6], &vnr[7], &vnr[8] );
			if (vars < 9) 
			{
				vars = sscanf( buffer + 2, "%i/%i %i/%i %i/%i", &vnr[0], &vnr[2], &vnr[3], &vnr[5], &vnr[6], &vnr[8] );
				if (vars < 6) sscanf( buffer + 2, "%i//%i %i//%i %i//%i", &vnr[0], &vnr[2], &vnr[3], &vnr[5], &vnr[6], &vnr[8] );
			}
			for ( unsigned int i = 0; i < 3; i++ )
			{
				v[i] = &vertex[vidx++];
				if (t)
				{
					unsigned int vidx = vnr[i * 3 + 1] - 1;
					cu[i] = tu[vidx], cv[i] = tv[vidx];
				}
				unsigned int nidx = vnr[i * 3 + 2] - 1, vidx = vnr[i * 3] - 1;
				v[i]->SetNormal( norm[nidx] );
				v[i]->SetPos( vert[vidx] );
			}
			Primitive* p = &m_Prim[m_Primitives++];
			if (t)
			{
				v[0]->SetUV( cu[0] * t->m_Width, cv[0] * t->m_Height );
				v[1]->SetUV( cu[1] * t->m_Width, cv[1] * t->m_Height );
				v[2]->SetUV( cu[2] * t->m_Width, cv[2] * t->m_Height );
			}
			p->Init( v[0], v[1], v[2] );
			p->SetMaterial( curmat );
			break;
		}
		case 'g':
			sscanf( buffer + 2, "%s", objname );
			strcpy( currobject, objname );
			break;
		case 'm':
			if (!_strnicmp( buffer, "mtllib", 6 ))
			{
				char libname[128];
				sscanf( buffer + 7, "%s", libname );
				char fullname[256];
				strcpy( fullname, path );
				strcat( fullname, libname );
				m_MatMan->LoadMTL( fullname );
			}
			break;
		case 'u':
			if (!_strnicmp( buffer, "usemtl", 6 ))
			{
				char matname[128];
				sscanf( buffer + 7, "%s", matname );
				curmat = m_MatMan->FindMaterial( matname );
			}
			break;
		default:
			break;
		}
	}
	fclose( f );
	delete vert;
	delete norm;
	delete tu;
	delete tv;
}

void Scene::InitSceneState()
{
	m_Extends.bmin = make_float3( 1000, 1000, 1000 );
	m_Extends.bmax = make_float3( -1000, -1000, -1000 );
	scene = this;
}

bool Scene::InitScene( const char* a_File )
{
	InitSceneState();
	if (!path)
	{
		path = new char[1024];
		file = new char[1024];
		noext = new char[1024];
	}
	strcpy( path, a_File );
	char* pos = path;
	while (strstr( pos, "/" )) pos = strstr( pos, "/" ) + 1;
	while (strstr( pos, "\\" )) pos = strstr( pos, "\\" ) + 1;
	*pos = 0;
	pos = (char*)a_File;
	while (strstr( pos, "/" )) pos = strstr( pos, "/" ) + 1;
	while (strstr( pos, "\\" )) pos = strstr( pos, "\\" ) + 1;
	if ((strstr( a_File, "/" )) || (strstr( a_File, "\\" )))
	{
		strcpy( file, pos );
	}
	else
	{
		strcpy( file, a_File );
	}
	LoadOBJ( a_File );
	strcpy( noext, file );
	pos = noext;
	while (strstr( pos + 1, "." )) pos = strstr( pos, "." );
	if (*pos == '.') *pos = 0;
	return m_Primitives > 0;
}

void Scene::UpdateSceneExtends()
{
	float3 emin = make_float3( 10000, 10000, 10000 ), emax = make_float3( -10000, -10000, -10000 );
	for ( unsigned int j = 0; j < m_Primitives; j++ ) for ( int v = 0; v < 3; v++ )
	{
		float3 pos = m_Prim[j].GetVertex( v )->GetPos();
		for ( int a = 0; a < 3; a++ )
		{
			if (pos.e[a] < emin.e[a]) emin.e[a] = pos.e[a];
			if (pos.e[a] > emax.e[a]) emax.e[a] = pos.e[a];
		}
	}
	m_Extends.bmin = emin, m_Extends.bmax = emax;
}

// EOF