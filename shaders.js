const shader_source_rand = `
	float Rand( int n, int seed )
	{
		n = (n<<13) ^ (n+seed);
		n = (n * (n * n * 15731 + 789221) + 1376312589) + n;
		return float(n & 0x7fffffff) / 2147483648.0;
	}
`;

//----------------------------------------------------------------------

const shader_source_shadow = `
	uniform mediump sampler2DShadow objShadMap;
	uniform mediump sampler2D hairShadMap;
	uniform float hairDensity;

	#define AMBIENT 0.1

	float bilinear( float v[4], vec2 f )
	{
		return mix( mix( v[0], v[1], f.x ), mix( v[2], v[3], f.x ), f.y );
	}

	vec3 bilinear( vec3 v[4], vec2 f )
	{
		return mix( mix( v[0], v[1], f.x ), mix( v[2], v[3], f.x ), f.y );
	}

	vec3 HairShadow( out vec3 spread, out float directF, vec3 pShad )
	{
		ivec2 size = textureSize( hairShadMap, 0 );
		vec2 t = pShad.xy * vec2(size) + 0.5;
		vec2 f = t - floor(t);
		vec2 s = 0.5 / vec2(size);
		
		vec2 tcp[4];
		tcp[0] = pShad.xy + vec2( -s.x, -s.y );
		tcp[1] = pShad.xy + vec2(  s.x, -s.y );
		tcp[2] = pShad.xy + vec2( -s.x,  s.y );
		tcp[3] = pShad.xy + vec2(  s.x,  s.y );

		const float coverage = 0.05;
		const vec3 a_f = vec3( 0.507475266, 0.465571405, 0.394347166 );
		const vec3 w_f = vec3( 0.028135575, 0.027669785, 0.027669785 );
		float dir[4];
		vec3 spr[4], t_d[4];
		for ( int i=0; i<4; ++i ) {
			float z = texture(hairShadMap, tcp[i]).r;
			//vis[i] = z < pShad.z ? 0.0 : 1.0;
			float h = max( 0.0, pShad.z - z );
			float n = h * hairDensity;
			dir[i] = pow( 1.0 - coverage, n );
			t_d[i] = pow( 1.0 - coverage*(1.0 - a_f), vec3(n,n,n) );
			spr[i] = n * coverage * w_f;
		}
		
		directF = bilinear( dir, f );
		spread  = bilinear( spr, f );
		return    bilinear( t_d, f );
	}
	
	vec3 Shadow( out vec3 spread, out float directF, vec3 pLight, float bias )
	{
		vec3 pShad = pLight * 0.5 + 0.5;
		pShad.z -= bias;
		float visObj  = texture(objShadMap, pShad);
		vec3 trans_direct = HairShadow( spread, directF, pShad );
		directF *= visObj;
		return trans_direct * visObj;
	}
`;

//----------------------------------------------------------------------

const shader_source_hair = shader_source_shadow + shader_source_rand + `
	uniform highp sampler2D N_TT_R;
	uniform highp sampler2D N_TRT;
	uniform highp sampler2D N_TT_R_GI;
	uniform highp sampler2D N_TRT_GI;
	uniform highp sampler3D GI;
	uniform highp sampler2D M_GI;
	
	//uniform float brightness;
	
	#define PI 3.1415926535897932384626433832795f
	#define ONE_OVER_PI      (1.0f / PI)
	#define ONE_OVER_PI_HALF (2.0f / PI)
	#define DEG2RAD(x) ((x) / 180.0f * PI)
	#define R_SHIFT      DEG2RAD(-5.0)
	#define TT_SHIFT     (-R_SHIFT*0.5f)
	#define TRT_SHIFT    (-R_SHIFT*1.5f)
	#define R_DEV        DEG2RAD(10.0)
	#define TT_DEV       DEG2RAD(10.0)
	#define TRT_DEV      DEG2RAD(18.0)
	#define ECCEN_SHIFT  DEG2RAD(25.0)
	
	#define SCALE_N_R      0.25
	#define SCALE_N_TT     0.5
	#define SCALE_N_TRT    0.25
	#define SCALE_N_R_GI   0.0625
	#define SCALE_N_TT_GI  0.66666666666666666666666666666667
	#define SCALE_N_TRT_GI 0.03703703703703703703703703703704
	#define SCALE_M_GI_R   2.6315789473684210526315789473684
	#define SCALE_M_GI_TT  2.2222222222222222222222222222222
	#define SCALE_M_GI_TRT 1.5151515151515151515151515151515
	#define SCALE_GI       0.05
	
	#define DENSITY 0.7f
	#define REAL_INTENSITY 5.0 //30.0
	
	vec3 ShadeHair( vec3 pLight, vec3 tangent, vec3 w_o, int instanceID )
	{
		float directFraction;
		vec3 spread;
		vec3 trans_direct = Shadow( spread, directFraction, pLight, 0.0 );
		vec3 trans = trans_direct - directFraction;

		vec3 w_i = lightDir;
		
		// theta
		float sinTheta_i = dot(w_i,tangent);
		float theta_i    = asin( sinTheta_i );
		float sinTheta_o = dot(w_o,tangent);
		float theta_o    = asin( sinTheta_o );
		float theta_h    = theta_o + theta_i;
		float theta      = 0.5f * abs( theta_o - theta_i );

		// for multiple scattering texture lookups
		float ix_theta_h = theta_h * ONE_OVER_PI * 0.5f + 0.5f;
		float ix_theta   = theta   * ONE_OVER_PI_HALF;
		vec3  ix_spread  = sqrt(spread) * ONE_OVER_PI*0.5f;

		// phi
		vec3  azL     = normalize( w_i - sinTheta_i * tangent );
		vec3  azV     = normalize( w_o - sinTheta_o * tangent );
		float cosPhiH = dot( azL, azV );
		float phiH    = acos( cosPhiH );
		
		// Factor for non-circular fibers
		float randVal = Rand( instanceID, 3 );
		float phi_shift = ECCEN_SHIFT*(randVal-0.5f);
	//#ifdef PHI_SHIFT_CORRECTION
		if ( dot(tangent,cross(azL,azV)) < 0.0 ) phi_shift = - phi_shift;
	//#endif
		float phiTRT = abs( phiH + phi_shift );
		if ( phiTRT > 2.0f*PI ) phiTRT -= 2.0f*PI;
		if ( phiTRT > PI ) phiTRT = 2.0f*PI - phiTRT;

		//////////////////////////////////////////////////////////////////////////
		// Direct Illumination
		//////////////////////////////////////////////////////////////////////////

		// N for direct
		vec2 index  = vec2( phiH * ONE_OVER_PI, ix_theta );
		vec2 index2 = vec2( phiTRT * ONE_OVER_PI, index.y );
		
		vec4  NTTR = texture( N_TT_R, index );
		float NR   = SCALE_N_R   * NTTR.a;
		vec3  NTT  = SCALE_N_TT  * NTTR.rgb;
		vec3  NTRT = SCALE_N_TRT * texture( N_TRT, index2 ).rgb;

		// M for direct
		float thetaR = theta_h - R_SHIFT;
		float MR = exp(-0.5f*thetaR*thetaR/(R_DEV*R_DEV)) / (sqrt(2.0f*PI)*R_DEV);

		float thetaTT = theta_h - TT_SHIFT;
		float MTT = exp(-0.5f*thetaTT*thetaTT/(TT_DEV*TT_DEV)) / (sqrt(2.0f*PI)*TT_DEV);

		float thetaTRT = theta_h - TRT_SHIFT;
		float MTRT = exp(-0.5f*thetaTRT*thetaTRT/(TRT_DEV*TRT_DEV)) / (sqrt(2.0f*PI)*TRT_DEV);

		vec3 color = MR*NR + MTT*NTT + MTRT*NTRT;
		
		//////////////////////////////////////////////////////////////////////////
		// Local Scattering
		//////////////////////////////////////////////////////////////////////////

		vec3 gi;
		gi.r = DENSITY * texture( GI, vec3( ix_spread.r, ix_theta_h, ix_theta ) ).r;
		gi.g = DENSITY * texture( GI, vec3( ix_spread.g, ix_theta_h, ix_theta ) ).g;
		gi.b = DENSITY * texture( GI, vec3( ix_spread.b, ix_theta_h, ix_theta ) ).b;

		color += gi;
		
		color *= directFraction;

		//////////////////////////////////////////////////////////////////////////
		// Global Scattering
		//////////////////////////////////////////////////////////////////////////

		// N for indirect
		vec4  nttr_gi = texture( N_TT_R_GI, index );
		float nr_gi   = nttr_gi.a;
		vec3  ntt_gi  = nttr_gi.rgb;
		vec3  ntrt_gi = texture( N_TRT_GI, index2 ).rgb;

		vec3 MGIr = texture( M_GI, vec2(ix_spread.r,ix_theta_h) ).xyz;
		vec3 MGIg = texture( M_GI, vec2(ix_spread.g,ix_theta_h) ).xyz;
		vec3 MGIb = texture( M_GI, vec2(ix_spread.b,ix_theta_h) ).xyz;
		vec3 mr_gi   = vec3( MGIr.x, MGIg.x, MGIb.x );
		vec3 mtt_gi  = vec3( MGIr.y, MGIg.y, MGIb.y );
		vec3 mtrt_gi = vec3( MGIr.z, MGIg.z, MGIb.z );

		vec3 f_indir = trans*DENSITY * ( gi + mr_gi*nr_gi + mtt_gi*ntt_gi + mtrt_gi*ntrt_gi );	// N components already divided by PI
		color += f_indir;

		//////////////////////////////////////////////////////////////////////////

		color += gi * AMBIENT;
		//color *= brightness;
		color *= REAL_INTENSITY;
	
		// gamma correction
		//color = pow( color, 1.0f/GAMMA );
		color = sqrt(color);
		
		return color;
	}
`;
//----------------------------------------------------------------------

const vs_hair_source_pre = `#version 300 es
	precision highp float;

	in vec4 pos;
	in vec4 pos2;
	in vec3 texW;
	
	uniform highp sampler3D tex_hairmesh;
	uniform highp sampler2D tex_uv;
	uniform highp sampler3D tex_uw;
	uniform highp sampler3D tex_v;
	uniform highp sampler3D tex_noise;
	uniform highp sampler2D tex_clump;

	uniform mat4 mvp;
	uniform mat4 mv;
	uniform mat3 mLight;
	uniform vec2 st;

	uniform float lengthVar;
	
	uniform float clumpSize;
	uniform float clumpExp;
	uniform float clumpVar;
	uniform float clumpPercent;
	uniform float clumpNoise;
	uniform float clumpFreq;

	uniform float curlAmount;
	uniform float curlFreq;
	uniform float curlTwists;
	uniform float curlTwistExp;
	uniform float curlExp;

	uniform float frizzAmount;
	uniform float frizzFreq;
	uniform float frizzExp;

	uniform float kinkAmount;
	uniform float kinkFreqUV;
	uniform float kinkFreqW;
	uniform float kinkWScale;
	uniform float kinkExp;
	
	uniform float fuzzAmount;
	uniform float fuzzDistrib;
	uniform float fuzzExp;

	out vec4 pView;
	out vec3 tView;
	out vec3 pLight;
	out float wPos;
	out float wMax;
	flat out int instanceID;
	out vec3 debugColor;

	` + shader_source_rand + `
	
	#define PI 3.1415926535897932384626433832795f
	
	vec3 GetPosFromHairMesh( vec2 stp, vec3 pos )
	{
		vec3 p0 = texture( tex_hairmesh, vec3(stp, pos.x) ).rgb;
		vec3 p1 = texture( tex_hairmesh, vec3(stp, pos.y) ).rgb;
		return mix( p0, p1, pos.z );
	}
	
	vec3 Noise( vec3 tc )
	{
		return texture(tex_noise, tc).rgb * 2.0 - 1.0;
	}
	
	float EvalHairVert( out vec3 p, out vec3 p2, vec4 pos, vec4 pos2, vec2 texW )
	{
		ivec3 size = textureSize( tex_hairmesh, 0 );
		vec2 stp = ( st + vec2( Rand(gl_InstanceID,0), Rand(gl_InstanceID,1) ) ) / vec2(size.xy);
		
		vec2 uv   = texture( tex_uv, stp ).rg;
		vec4 uw   = texture( tex_uw, vec3(stp,texW.x) );
		vec3 vDir = texture( tex_v,  vec3(stp,texW.x) ).rgb;
		vec3 uDir = uw.xyz;
		vec3 wDir = normalize( cross(uDir, vDir) );
		mat3 style2obj;
		style2obj[0] = normalize(vDir);
		style2obj[1] = normalize(uDir);
		style2obj[2] = normalize(wDir);
		
		float w  = uw.w; //pos.w;
		float w2 = uw.w + pos2.w; //pos.w + pos2.w;
		
		p  = GetPosFromHairMesh( stp, pos.xyz  );
		p2 = GetPosFromHairMesh( stp, pos2.xyz );
		
		// Clump
		vec2 cuv = uv;
		if ( Rand(gl_InstanceID,2) < clumpPercent ) {
			ivec2 ctsize = textureSize( tex_clump, 0 );
			vec2 duv = clumpNoise * Noise( vec3(uv*clumpFreq,0.0) ).xy / vec2(ctsize);
			vec4 clumpPP = texture( tex_clump, uv + duv );
			vec2 stc = clumpPP.xy + clumpPP.zw / 256.0;
			float cDist = length(stc - stp);
			vec3 cp  = GetPosFromHairMesh( stc, pos.xyz  );
			vec3 cp2 = GetPosFromHairMesh( stc, pos2.xyz );
			vec3 cDiff  = cp  - p;
			vec3 cDiff2 = cp2 - p2;
			p  += cDiff * (1.0f - cDist * clumpSize / length(cDiff )) * pow( w,  clumpExp );
			p2 += cDiff * (1.0f - cDist * clumpSize / length(cDiff2)) * pow( w2, clumpExp );
			cuv = texture( tex_uv, stc ).rg;
			uv = mix( cuv, uv, clumpVar );
		}
		
		// Kink
        vec3 dStyle  = Noise( vec3(uv*kinkFreqUV, w *kinkFreqW) );
        vec3 dStyle2 = Noise( vec3(uv*kinkFreqUV, w2*kinkFreqW) );
		dStyle  *= kinkAmount * pow( w,  kinkExp );
		dStyle2 *= kinkAmount * pow( w2, kinkExp );
		dStyle.z  *= kinkWScale;
		dStyle2.z *= kinkWScale;

		// Curl
		float curlNoise = Noise( vec3(cuv*curlFreq,0.0) ).z;
		float curlAngle  = curlTwists * ( pow( w,  curlTwistExp ) + curlNoise );
		float curlAngle2 = curlTwists * ( pow( w2, curlTwistExp ) + curlNoise );
		dStyle.xy  += vec2( cos(curlAngle), sin(curlAngle) ) * curlAmount * pow( w,  curlExp );
		dStyle2.xy += vec2( cos(curlAngle), sin(curlAngle) ) * curlAmount * pow( w2, curlExp );

		// Frizz
        vec2 frizz = Noise( vec3(uv*frizzFreq,0.0) ).rg;
		dStyle.xy  += frizz * ( frizzAmount * pow( w,  frizzExp ) );
		dStyle2.xy += frizz * ( frizzAmount * pow( w2, frizzExp ) );

		// Fuzz
		float phi = (2.0*PI) * Rand(gl_InstanceID,3);
		float s = fuzzAmount * pow( Rand(gl_InstanceID,4), fuzzDistrib );
		vec2 fuzz = vec2(cos(phi), sin(phi)) * s;
		dStyle.xy  += fuzz * pow( w,  fuzzExp );
		dStyle2.xy += fuzz * pow( w2, fuzzExp );
		
		p  += style2obj*dStyle;
		p2 += style2obj*dStyle2;
		
		return w;
	}

	void main()
	{
		vec3 p, p2;
		float w = EvalHairVert( p, p2, pos, pos2, texW.xy );

		vec3 t  = (p2-p)*pos2.w;
		vec4 glpos = mvp * vec4(p,1);
		pView  =  mv * vec4(p,1);
		tView  = (mv * vec4(t,0)).xyz;
		pLight = mLight * p;
		wPos = w;
		wMax = 1.0 - lengthVar * Rand(gl_InstanceID,5);
		instanceID = gl_InstanceID;
		debugColor = vec3(w,w,w);
`;

const vs_hair_source_post = `
		gl_Position = glpos;
	}
`;
const vs_hair_line_source = vs_hair_source_pre + vs_hair_source_post;

const vs_hair_tri_source = vs_hair_source_pre + `
		vec2 b = normalize( vec2( -tView.y, tView.x ) );
		glpos.xy += b * texW.z * glpos.w;
	` + vs_hair_source_post;


const fs_hair_source = `#version 300 es
	precision highp float;

	uniform vec3 lightDir;

	in vec4 pView;
	in vec3 tView;
	in vec3 pLight;
	in float wPos;
	in float wMax;
	flat in int instanceID;
	in vec3 debugColor;

	out vec4 theColor;

	` + shader_source_hair + `

	void main()
	{
		if ( wPos > wMax ) discard;
		vec3 v = normalize( -pView.xyz / pView.w );
		//float dwx = dFdx(w);
		//float dwy = dFdy(w);
		//vec4  dvx = dFdx(pView);
		//vec4  dvy = dFdy(pView);
		//float dw = abs(dwx) > abs(dwy) ? dwx : dwy;
		//vec4  dv = abs(dwx) > abs(dwy) ? dvx : dvy;
		//vec4 dv_dw = dv / dw;
		//vec3 t = normalize( (pView.xyz + dv_dw.xyz) / (pView.w + dv_dw.w) - pView.xyz/pView.w );
		vec3 t = normalize(tView);
		vec3 pShad = pLight * 0.5 + 0.5;
		vec3 color = ShadeHair( pLight, t, v, instanceID );
		theColor = vec4(color,1);
		
		//theColor = vec4(debugColor,1);
		//vec3 f = debugColor*16.0 - floor(debugColor*16.0);
		//theColor = vec4(f,1);

		//float ambient = 0.1;
		//float shad = Shadow( pLight, 0.0 );
		//float s = sqrt( 1.0 - tangent.z*tangent.z );
		//theColor = vec4( vec3(1,1,1)*(s*shad + ambient), 1 );
		//theColor = vec4( tangent, 1 );
		//theColor = vcolor;
		//theColor = texture( tex_hairmesh, vec3(texCoord, 0.5, 0.5) );
	}
`;

//----------------------------------------------------------------------

const vs_hair_strip_source = `#version 300 es
	precision highp float;

	in vec4 pos;
	in vec4 pos2;
	in vec3 texW;
	
	uniform highp sampler3D tex_hairmesh;
	uniform highp sampler2D tex_uv;
	uniform highp sampler3D tex_uw;
	uniform highp sampler3D tex_v;
	
	uniform mat4 mvp;
	uniform mat4 mv;
	uniform mat3 mLight;
	uniform vec4 st;

	out vec4 pView;
	out vec3 tView;
	out vec3 pLight;
	out float wPos;
	out float wMax;
	flat out int instanceID;
	out vec3 debugColor;

	` + shader_source_rand + `

	#define PI 3.1415926535897932384626433832795f

	vec3 GetPosFromHairMesh( vec2 stp, vec3 pos )
	{
		vec3 p0 = texture( tex_hairmesh, vec3(stp, pos.x) ).rgb;
		vec3 p1 = texture( tex_hairmesh, vec3(stp, pos.y) ).rgb;
		return mix( p0, p1, pos.z );
	}
	
	uniform vec3 stripShift;
	
	void main()
	{
		ivec3 size = textureSize( tex_hairmesh, 0 );
		//vec2 stp = ( st.xy + vec2( Rand(gl_InstanceID,0), Rand(gl_InstanceID,1) ) ) / vec2(size.xy);
		vec2 stp = ( st.xy + st.zw ) / vec2(size.xy);
		
		//vec2 uv   = texture( tex_uv, stp ).rg;
		vec4 uw   = texture( tex_uw, vec3(stp,texW.x) );
		vec3 vDir = texture( tex_v,  vec3(stp,texW.x) ).rgb;
		vec3 uDir = uw.xyz;
		//vec3 wDir = normalize( cross(uDir, vDir) );
		float w = uw.w;

		/*
		float s = st.z + float(gl_InstanceID) * st.w;
		s -= floor(s);
		s *= 2.0;
		ivec3 size = textureSize( tex_hairmesh, 0 );
		vec2 ss = vec2( min(s,1.0), max(0.0,s-1.0) );
		if ( gl_VertexID % 2 == 1 ) ss = vec2(1,1) - ss; 
		vec2 stp = ( st.xy + ss ) / vec2(size.xy);
		float w = texture( tex_uw, vec3(stp,texW.x) ).w;
		*/
				
		vec3 p  = GetPosFromHairMesh( stp, pos.xyz  );
		vec3 p2 = GetPosFromHairMesh( stp, pos2.xyz );
		
		vec3 t  = (p2-p)*pos2.w;
		
		float phi = (2.0*PI) * Rand(gl_InstanceID,3);
		vec2 r = vec2(cos(phi), sin(phi)) * texW.z * 10.0;
		p += r.x * uDir + r.y * vDir;

		vec4 glpos = mvp * vec4(p,1);
		pView  =  mv * vec4(p,1);
		tView  = (mv * vec4(t,0)).xyz;
		pLight = mLight * p;
		wPos = w;
		wMax = 1.0; // - lengthVar * Rand(gl_InstanceID,5);
		instanceID = gl_InstanceID;
		//w = float(gl_InstanceID) * st.w;
		debugColor = vec3(w,w,w);

		gl_Position = glpos;
	}
`;

//----------------------------------------------------------------------

const vs_clump_source = `#version 300 es
	in vec2 pos;
	uniform highp sampler2D tex_uv;
	uniform highp sampler3D tex_noise;
	uniform vec2 st;
	uniform float clumpFreq;
	uniform float noiseScale;
	out vec2 stp;
	void main()
	{
		ivec2 size = textureSize( tex_uv, 0 );
		stp = ( st + pos ) / vec2(size);
		vec2 uv = texture( tex_uv, stp ).rg;
		vec2 d  = texture( tex_noise, vec3(uv*clumpFreq,0.0) ).rg * 2.0 - 1.0;
		uv += d * noiseScale;
		gl_Position = vec4( uv*2.0 - 1.0, 0.0, 1.0 );
	}
`;

const fs_clump_source = `#version 300 es
	precision highp float;
	in  vec2 stp;
	out vec4 theColor;
	void main()
	{
		// encode stp as (s0*256 + s1)/256
		vec2 s0 = floor(stp*256.0) / 256.0;
		vec2 s1 = stp*256.0 - floor(stp*256.0);
		theColor = vec4(s0,s1);
	}
`;

const fs_clumpExt_source = `#version 300 es
	precision highp float;
	uniform highp sampler2D tex_clump;
	in  vec2 uv;
	out vec4 theColor;
	void main()
	{
		ivec2 size = textureSize( tex_clump, 0 );
		vec2 d = 1.0 / vec2(size);

		theColor = texture( tex_clump, uv );                             if ( theColor != vec4(0,0,0,0) ) return;
		theColor = texture( tex_clump, vec2( uv.x - d.x, uv.y ) );       if ( theColor != vec4(0,0,0,0) ) return;
		theColor = texture( tex_clump, vec2( uv.x + d.x, uv.y ) );       if ( theColor != vec4(0,0,0,0) ) return;
		theColor = texture( tex_clump, vec2( uv.x, uv.y - d.y ) );       if ( theColor != vec4(0,0,0,0) ) return;
		theColor = texture( tex_clump, vec2( uv.x, uv.y + d.y ) );       if ( theColor != vec4(0,0,0,0) ) return;
		theColor = texture( tex_clump, vec2( uv.x - d.x, uv.y - d.y ) ); if ( theColor != vec4(0,0,0,0) ) return;
		theColor = texture( tex_clump, vec2( uv.x + d.x, uv.y - d.y ) ); if ( theColor != vec4(0,0,0,0) ) return;
		theColor = texture( tex_clump, vec2( uv.x - d.x, uv.y + d.y ) ); if ( theColor != vec4(0,0,0,0) ) return;
		theColor = texture( tex_clump, vec2( uv.x + d.x, uv.y + d.y ) );
	}
`;

//----------------------------------------------------------------------

const vs_hairmeshC_source = `#version 300 es
	in vec4 pos;
	uniform mat4 mvp;
	uniform highp sampler3D tex_hairmesh;
	void main()
	{
		ivec3 size = textureSize( tex_hairmesh, 0 );
		int ix = gl_InstanceID % size.x;
		int iy = gl_InstanceID / size.x;
		vec2 uvp = vec2( (float(ix)+0.5)/float(size.x), (float(iy)+0.5)/float(size.y) );
		vec3 p0 = texture( tex_hairmesh, vec3(uvp, pos.x) ).rgb;
		vec3 p1 = texture( tex_hairmesh, vec3(uvp, pos.y) ).rgb;
		vec3 p = mix( p0, p1, pos.z );
		gl_Position = mvp * vec4(p,1);
	}
`;

const vs_hairmeshL_source = `#version 300 es
	in vec2 pos;
	uniform mat4 mvp;
	uniform highp sampler3D tex_hairmesh;
	void main()
	{
		ivec3 size = textureSize( tex_hairmesh, 0 );
		float z = (float(gl_InstanceID*3) + 0.5) / float(size.z);
		vec3 p = texture( tex_hairmesh, vec3(pos,z) ).rgb;
		gl_Position = mvp * vec4(p,1);
	}
`;

const fs_hairmesh_source = `#version 300 es
	precision lowp float;
	out vec4 theColor;
	void main()
	{
		theColor = vec4(0,1,1,1);
	}
`;

//----------------------------------------------------------------------

const vs_obj_source = `#version 300 es

	in vec3 pos;
	in vec3 nrm;
	in vec2 uvs;

	uniform mat4 mvp;
	uniform mat4 mv;
	uniform mat3 mLight;

	out vec4 pView;
	out vec3 nView;
	out vec3 pLight;
	out vec3 nLight;
	out vec2 uv;

	void main()
	{
		vec4 p = vec4(pos,1);
		gl_Position = mvp * vec4(pos,1);
		pView  =  mv * vec4(pos,1);
		nView  = (mv * vec4(nrm,0)).xyz;
		pLight = mLight * pos;
		nLight = mLight * nrm;
		uv = uvs;
	}
`;

const fs_obj_source = `#version 300 es
	precision mediump float;

	uniform mediump sampler2D tex_obj;
	uniform vec3 lightDir;

	in vec4 pView;
	in vec3 nView;
	in vec3 pLight;
	in vec3 nLight;
	in vec2 uv;
	
	out vec4 theColor;

	` + shader_source_shadow + `

	void main()
	{
		vec3 n = normalize(nView);
		if ( !gl_FrontFacing ) n = -n;
		float d = dot(n,lightDir);
		float tanT = sqrt(1.0 - d*d)/d;
		float directFraction;
		vec3 spread;
		vec3 vis = Shadow( spread, directFraction, pLight, 0.005*tanT + 0.0005 );
		vec3 diffuse_color = texture(tex_obj,uv).rgb;
		float diffuse = max(0.0,d);
		float specular = 0.0;
		if ( diffuse > 0.0 && diffuse_color.r > 0.0 ) {
			vec3 v = normalize( -pView.xyz / pView.w );
			vec3 h = normalize( v + lightDir );
			float s = max(0.0,dot( n, h ));
			specular = pow( s, 20.0 ) * 0.3;
		}
		vec3 color = (vis*diffuse + AMBIENT)*diffuse_color + vis*specular;
		theColor = vec4(color,1);
	}
`;

//----------------------------------------------------------------------

const vs_light_source = `#version 300 es
	in vec3 pos;

	uniform mat4 mvp;
	uniform mat3 mLight;
	out float vis;

	void main()
	{
		mat3 light2Obj = inverse(mLight);
		vec3 p = light2Obj * pos;
		gl_Position = mvp * vec4(p,1);
		vis = abs(pos.z);
	}
`;

const fs_light_source = `#version 300 es
	precision lowp float;
	in float vis;
	out vec4 theColor;
	void main()
	{
		theColor = vec4(1,1,1,vis);
	}
`;

//----------------------------------------------------------------------

const fs_shadow_source = `#version 300 es
	precision lowp float;
	out vec4 theColor;
	void main()
	{
		theColor = vec4(1,1,1,1);
	}
`;

const fs_shadow_hair_source = `#version 300 es
	precision lowp float;
	in float wPos;
	in float wMax;
	out vec4 theColor;
	void main()
	{
		if ( wPos > wMax ) discard;
		theColor = vec4(1,1,1,1);
	}
`;

//----------------------------------------------------------------------

const vs_tex_source = `#version 300 es
	in vec2 pos;
	out vec2 uv;
	void main()
	{
		uv = pos;
		gl_Position = vec4( pos*2.0 - 1.0, 0.0, 1.0 );
	}
`;

const fs_tex_source = `#version 300 es
	precision highp float;
	uniform highp sampler2D tex;
	in  vec2 uv;
	out vec4 theColor;
	void main()
	{
		theColor = texture( tex, uv );
	}
`;

//----------------------------------------------------------------------

