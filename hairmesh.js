const resolution = 1024;
const numHairMeshCurveVerts = 32;
const depthTextureSize = resolution;

var numHair = 10000;
var numHairVerts = 32;
var renderMode = 0;
var hairWidth = 1.5;
var stripWidth = 4.0;
var numStrips = 200;

var hairParam = new Array();

const debug = false;
const showAdvanced = false;

//----------------------------------------------------------------------

var canvas = false;
var simulator = {
	init      : function() {},
	run       : function() {},
	isRunning : function() { return false; },
};
var camTilt = -0.1;
var camDist = 2.5;
var objRot  = 2.5;
var lightTilt = -0.9;
var lightRot  = -0.5;
var lightScale = 1;
var pendingTextures = 0;
var modelShift = [0,-0.2,0];

var actionCamera = {
	activate: function(e) { this.x = e.clientX; this.y = e.clientY; this.active = true; },
	deactivate: function(e) { this.active = false; },
	apply: function(e) {
		if ( this.active ) {
			objRot  += (this.x - e.clientX) * 2 / canvas.clientWidth;
			camTilt += (this.y - e.clientY) * 2 / canvas.clientHeight;
			if ( camTilt >  Math.PI/2 ) camTilt =  Math.PI/2;
			if ( camTilt < -Math.PI/2 ) camTilt = -Math.PI/2;
			this.x = e.clientX;
			this.y = e.clientY;
			simulator.run();
			Refresh();
		}
	},
};

var actionLight = {
	activate: function(e) { this.x = e.clientX; this.y = e.clientY; this.active = true; },
	deactivate: function(e) { this.active = false; },
	apply: function(e) {
		if ( this.active ) {
			lightRot  -= (this.x - e.clientX) * 2 / canvas.clientWidth;
			lightTilt -= (this.y - e.clientY) * 2 / canvas.clientHeight;
			if ( lightTilt >  Math.PI/2 ) lightTilt =  Math.PI/2;
			if ( lightTilt < -Math.PI/2 ) lightTilt = -Math.PI/2;
			this.x = e.clientX;
			this.y = e.clientY;
			Refresh();
		}
	},
};

var mouseAction = false;

function mouseDown(e)
{
	if ( e.ctrlKey ) {
		mouseAction = actionLight;
	} else {
		mouseAction = actionCamera;
	}
	mouseAction.activate(e);
}

function mouseMove(e)
{
	if ( mouseAction ) {
		mouseAction.apply(e);
	}
}

function mouseUp(e)
{
	if ( mouseAction ) mouseAction.deactivate(e);
	mouseAction = false;
}

var activeTouches = [-1, -1];

function touchDown(e)
{
	e.stopPropagation();
	e.preventDefault();
	for ( let i=0; i<e.changedTouches.length; ++i ) {
		let touch = e.changedTouches[i];
		if      ( activeTouches[0] < 0 ) { activeTouches[0]=touch.identifier; actionCamera.activate(touch); }
		else if ( activeTouches[1] < 0 ) { activeTouches[1]=touch.identifier; actionLight .activate(touch); }
	}
}

function touchMove(e)
{
	e.stopPropagation();
	e.preventDefault();
	for ( let i=0; i<e.changedTouches.length; ++i ) {
		let touch = e.changedTouches[i];
		if ( touch.identifier == activeTouches[0] ) actionCamera.apply(touch);
		if ( touch.identifier == activeTouches[1] ) actionLight .apply(touch);
	}
}

function touchUp(e)
{
	e.stopPropagation();
	e.preventDefault();
	for ( let i=0; i<e.changedTouches.length; ++i ) {
		let touch = e.changedTouches[i];
		if ( touch.identifier == activeTouches[0] ) { activeTouches[0]=-1; actionCamera.deactivate(touch); }
		if ( touch.identifier == activeTouches[1] ) { activeTouches[1]=-1; actionLight .deactivate(touch); }
	}
}

window.addEventListener("mouseup", mouseUp, false);

function Refresh()
{
	if ( !simulator.isRunning() ) {
		window.requestAnimationFrame(RenderScene);
	}
}

function SetProgUniform( progs, name, val )
{
	if ( ! Array.isArray(progs) ) progs = [ progs ];
	for ( let i=0; i<progs.length; ++i ) {
		gl.useProgram( progs[i] );
		gl.uniform1f( gl.getUniformLocation( progs[i], name ), val );
	}
	Refresh();
}

//----------------------------------------------------------------------

var paramBlock = false;

function AddParamSeperator( label='', checkname=false )
{
	paramBlock = false;
	let tr = document.createElement('tr');
	let td = document.createElement('td');
	let h = document.createElement('hr');
	h.className = label.length > 0 ? 'cSepWLabel' : 'cSep';
	td.colSpan = 2;
	td.appendChild(h);
	tr.appendChild(td);
	addControlTarget.appendChild(tr);
	if ( label.length > 0 ) {
		let t = document.createElement('span');
		if ( checkname ) {
			let cCheck = document.createElement('input');
			cCheck.type = 'checkbox';
			cCheck.id = checkname;
			cCheck.checked = true;
			cCheck.elemList = [];
			cCheck.cInput = false; 
			cCheck.onchange = function() {
				let d=!this.checked;
				this.elemList.forEach( function(c) {
					c.disabled = d;
					if ( d ) c.setAttribute('disabled','disabled');
					else c.removeAttribute('disabled');
				});
				if ( this.elemList.length > 0 ) {
					if ( d ) {
						this.cInputValue = this.elemList[0].value;
						this.elemList[0].value = 0;
					} else {
						this.elemList[0].value = this.cInputValue;
					}
					this.elemList[0].update();
				}
			};
			td.appendChild(cCheck);
			paramBlock = cCheck;
			let cLabel = document.createElement('label');
			cLabel.htmlFor = checkname;
			cLabel.innerHTML = label;
			t.appendChild(cCheck);
			t.appendChild(cLabel);
		} else {
			t.innerHTML = label;
		}
		t.className = 'cGroupTitle';
		td.appendChild(t);
	}
}

function AddCheckbox( name, label, vdef, action = function(){ window[name] = this.checked; Refresh(); } )
{
	let cLabel = document.createElement('label');
	cLabel.htmlFor = name;
	let cInput = document.createElement('input');
	cInput.type = 'checkbox';
	cInput.id = name;
	cInput.checked = vdef;
	cInput.onchange = action;
	let cSpan = document.createElement('span');
	cSpan.innerHTML = ' ' + label;
	cLabel.appendChild(cInput);
	cLabel.appendChild(cSpan);
	cInput.onchange();
	
	let tr = document.createElement('tr');
	let td = document.createElement('td');
	td.colSpan = 2;
	td.appendChild(cLabel);
	tr.appendChild(td);
	addControlTarget.appendChild(tr);
	return cInput;
}

function AddSelect( name, label, values, vdef, action )
{
	let cLabel = document.createElement('label');
	cLabel.className = 'cName';
	cLabel.innerHTML = label + ':';
	let cSelect = document.createElement('select');
	cSelect.id = name;
	cSelect.onchange = action;
	for ( let i=0; i<values.length; ++i ) {
		let cOption = document.createElement('option');
		cOption.value     = values[i].value;
		cOption.innerHTML = values[i].label;
		if ( vdef == values[i].value ) cOption.selected = true;
		cSelect.appendChild(cOption);
	}
	cSelect.onchange();
	
	let tr = document.createElement('tr');
	let th = document.createElement('th');
	let td = document.createElement('td');
	//tr.className = 'control';
	th.appendChild(cLabel);
	td.appendChild(cSelect);
	tr.appendChild(th);
	tr.appendChild(td);
	addControlTarget.appendChild(tr);
	return cSelect;
}

function AddParam( name, label, vdef, 
	vmin=0, vmax=1, scale=1, 
	updateProgs = [ prog_hair, prog_shad_hair ],
	rmin=0, rmax=200,
	r2v = function(r){ return (vmax-vmin)*(r-rmin)/(rmax-rmin)+vmin; },
	v2r = function(v){ return (v-vmin)/(vmax-vmin)*(rmax-rmin)+rmin; } )
{
	let cLabel = document.createElement('label');
	cLabel.className = 'cName';
	cLabel.id = name+'L';
	cLabel.innerHTML = label + ':';
	let cSlider = document.createElement('input');
	cSlider.type = 'range';
	cSlider.id = name+'R';
	cSlider.className = 'cSlider';
	cSlider.min = rmin;
	cSlider.max = rmax;
	let rdef = v2r(vdef);
	cSlider.value  = rdef;
	cSlider.defVal = rdef;
	cSlider.resetFunc = function() { this.value=this.defVal; this.oninput(); };
	cSlider.ondblclick = cSlider.resetFunc;
	cLabel.cSlider = cSlider;
	cLabel.ondblclick = function() { this.cSlider.resetFunc(); }
	let cInput = document.createElement('input');
	cInput.type = 'number';
	cInput.id = name+'N';
	cInput.className = 'cSInput';
	cInput.min = vmin;
	cInput.max = vmax;
	cInput.cSlider = cSlider;
	cSlider.cInput = cInput;
	cSlider.r2v = r2v;
	cInput.r2v = r2v;
	cInput.v2r = v2r;
	cInput.target = name;
	cInput.step = (vmax-vmin)/(rmax-rmin);
	cInput.mapValue = function(v) { return v*scale; }
	cSlider.oninput = function() {
		this.cInput.value = Math.round( this.r2v( this.value ) * 1000 ) / 1000;
		this.cInput.update();
	};
	cInput.onchange = function() {
		this.cSlider.value = this.v2r( this.value );
		this.value = Math.round( this.r2v( cSlider.value ) * 1000 ) / 1000;
		this.update();
	}
	cInput.updateProgs = updateProgs;
	cInput.update = Array.isArray(updateProgs) ?
		function() { SetProgUniform( this.updateProgs, this.target, this.mapValue(this.value) ); } : 
		function() { this.updateProgs(this); Refresh(); };
	cSlider.oninput();
	
	if ( Array.isArray(updateProgs) && updateProgs[0] == prog_hair ) {
		hairParam.push( cInput );
	}

	let tr = document.createElement('tr');
	let th = document.createElement('th');
	let td = document.createElement('td');
	tr.id = name + 'TR';
	th.appendChild(cLabel);
	td.appendChild(cInput);
	td.appendChild(cSlider);
	tr.appendChild(th);
	tr.appendChild(td);
	addControlTarget.appendChild(tr);

	if ( paramBlock ) {
		paramBlock.elemList.push(cInput);
		paramBlock.elemList.push(cSlider);
		//paramBlock.elemList.push(cLabel);
		paramBlock.elemList.push(tr);
	}

	return cInput;
}

function AddParamLog( name, label, vdef, vmin, vmax, updateProgs, scale=1 )
{
	let cInput = AddParam( name, label, vdef, vmin, vmax, scale, updateProgs, 0, 200,
		function(r){ return Math.round( Math.pow( 10, (r-100)/100 + Math.log10(vdef) ) * 10/vmin ) / (10/vmin); },
		function(v){ return Math.round( Math.log10(v/vdef) * 100 + 100 ); } );
	cInput.onchange2 = cInput.onchange;
	cInput.onchange = function(e) {
		let v = this.value;
		let r0 = parseInt(this.cSlider.value);
		let v0 = this.r2v(r0);
		let r1 = ( v < v0 ) ? r0-1 : r0+1;
		let v1 = this.r2v(r1);
		if ( Math.abs(v-v0) < Math.abs(v1-v0) ) this.value = v1;
		this.onchange2(e);
	};
	cInput.step = vmin / 10;
}

function AddParamExp( name, label='root &harr; tip', vdef=0 )
{
	let cInput = AddParam( name, label, vdef, -1, 1 );
	cInput.mapValue = function(v) { return 2.0002/(1.0001-v) - 1; }
	cInput.onchange();
}

//----------------------------------------------------------------------

function ShowParam(id,show)
{
	let e = document.getElementById(id);
	if ( e ) e.style.display = show ? '' : 'none';
}

//----------------------------------------------------------------------

function InitParam()
{
	clumpRes   = 32;
	clumpNoise = 1;
	clumpFreq  = 1;

	addControlTarget = document.getElementById('cParam');

	AddParamSeperator('Parameters');
	
	AddCheckbox( 'showModel',     'show head',            true  );
	AddCheckbox( 'showHair',      'show hair',            true  );
	AddCheckbox( 'showHairMesh',  'show hair mesh',       false );
	AddCheckbox( 'showLightDir',  'show light direction', false );

	AddSelect( 'hairColor', 'hair color', [
		{ value: 'std_blond', label: 'Std. Blond' },
		{ value: 'mid_blond', label: 'Mid. Blond' },
		{ value: 'rosa',      label: 'Rosa'       },
		{ value: 'brown',     label: 'Brown'      },
	], 'std_blond', function() { SetHairMaterial(this.value); } );
	
	//AddParam   ( 'brightness',  'brightness',   5, 0,    10 );
	AddParamLog( 'hairDensity', 'shadows',      1, 0.1, 100, [ prog_hair, prog_obj ], 10000 );
	AddParam   ( 'lengthVar',   'length var.',  0.1 );

	AddParamSeperator('Fuzz','fuzz');
	AddParam   ( 'fuzzAmount',   'amount',       0.10, 0.0, 0.2 );
	AddParamExp( 'fuzzDistrib',  'distribution', 0.80 );
	AddParamExp( 'fuzzExp' );
	
	AddParamSeperator('Frizz','frizz');
	AddParam   ( 'frizzAmount',  'amount',       0.02, 0.0, 0.2 );
	AddParam   ( 'frizzFreq',    'frequency',    1.00, 0.0, 4.0 );
	AddParamExp( 'frizzExp' );

	AddParamSeperator('Kink','kink');
	AddParam   ( 'kinkAmount',   'amount',       0.02, 0.0, 0.2 );
	AddParam   ( 'kinkFreqUV',   'uv frequency', 2.00, 0.0, 4.0 );
	AddParam   ( 'kinkFreqW',    'w frequency',  1.00, 0.0, 2.0 );
	AddParam   ( 'kinkWScale',   'stretch',      0.50  );
	AddParamExp( 'kinkExp' );

	AddParamSeperator('Clump','clump');
	AddParam   ( 'clumpPercent', 'percentage',   0.90, 0.0, 1.0 );
	AddParam   ( 'clumpRes',     'resolution',   32, 1, 256, 1, function(v){ ResizeClumpTexture(v.value); }, 1, 256 );
	AddParam   ( 'clumpSize',    'size',         0.00, 0.0, 0.3 );
	AddParam   ( 'clumpNoise',   'noise',        1.00, 0.0, 1.0, 1, function(v){ clumpNoise=v.value; UpdateClumpTexture(); } );
	AddParam   ( 'clumpFreq',    'frequency',    1.00, 0.0, 4.0, 1, function(v){ clumpFreq =v.value; UpdateClumpTexture(); } );
	AddParam   ( 'clumpVar',     'variation',    0.10, 0.0, 1.0 );
	AddParamExp( 'clumpExp' );

	AddParamSeperator('Curl','curl');
	AddParam   ( 'curlAmount',   'amount',       0.02, 0.0, 0.2 );
	AddParam   ( 'curlFreq',     'frequency',    1.00, 0.0, 2.0 );
	AddParam   ( 'curlTwists',   'twists',      10.00, 0.0, 20.0, 2*Math.PI );
	AddParamExp( 'curlTwistExp', 'twist &harr;', 0.3 );
	AddParamExp( 'curlExp' );


	if ( showAdvanced ) {

		AddParamSeperator('Advanced');
		
		AddParamLog( 'hairCount',   'hair count',   numHair, 1000, 100000, function(input){ numHair = input.value; } );
		AddParam   ( 'hairSegs',    'hair segments', numHairVerts, 16, 128, 16, function(v){ numHairVerts=v.value; UpdateHairInstance(); }, 1, 8 );
		let selRenderMode = AddSelect( 'renderMode', 'render mode', [
			{ value: '0', label: 'Lines' },
			{ value: '1', label: 'Triangles' },
			//{ value: '2', label: 'Textured Strips' },
		], renderMode, function() {
			renderMode = parseInt(this.value);
			ShowParam('hairWidthTR',renderMode==1);
			ShowParam('stripWidthTR',renderMode==2);
			ShowParam('numStripsTR',renderMode==2);
			CompileHairShaders();
			UpdateHairInstance();
			UpdateClumpTexture();
			Refresh();
		});
		AddParam   ( 'hairWidth', 'hair width', hairWidth, 0.0, 10.0, 1, function(v){ hairWidth=v.value; UpdateHairInstance(); Refresh(); } );
		AddParam   ( 'stripWidth', 'strip width', stripWidth, 0.0, 10.0, 1, function(v){ stripWidth=v.value; UpdateHairInstance(); Refresh(); } );
		AddParam   ( 'numStrips', '# strips',   numStrips, 100, 1000, 10, function(v){ numStrips=v.value; Refresh(); }, 10, 100 );
		selRenderMode.onchange();
	}

	if ( debug ) {
		AddCheckbox( 'showLightView', 'show light view',      false );
		AddSelect( 'showTexture', 'show texture', [
			{ value: '',         label: 'None' },
			{ value: 'clump',    label: 'Clump Centers' },
			{ value: 'clumpExt', label: 'Clump Centers (extended)' },
			{ value: 'shadow',   label: 'Hair Shadow Map' },
		], '', function() { showTexture = this.value; Refresh(); } );
	} else {
		showLightView = false;
		showTexture = '';
	}

	AddParamSeperator();
}

//----------------------------------------------------------------------

function MatrixModel( rot )
{
	let cm = Math.cos(rot);
	let sm = Math.sin(rot);
	return [ cm,0,sm,0, 0,1,0,0, -sm,0,cm,0, 0,0,0,1 ];
}

function MatrixMV( rot, tilt, dist )
{
	let cm = Math.cos(rot);
	let sm = Math.sin(rot);
	let c  = Math.cos(tilt);
	let s  = Math.sin(tilt);
	return [cm,s*sm,c*sm,0, 0,c,-s,0, -sm,s*cm,c*cm,0, 0,0,-dist,1];
}

function MatrixMVP( rot, tilt, dist, fov=45, aspect=1 )
{
	let cm = Math.cos(rot);
	let sm = Math.sin(rot);
	let c  = Math.cos(tilt);
	let s  = Math.sin(tilt);
	let T = 1 / Math.tan(fov*Math.PI/360);
	return [T*aspect*cm,T*s*sm,-1.2*c*sm,-c*sm, 0,T*c,1.2*s,s, -T*aspect*sm,T*s*cm,-1.2*c*cm,-c*cm, 0,0,1.2*dist-2.2,dist];
}

function MVLightDir( obj_rot, cam_tilt, light_rot, light_tilt )
{
	let cm  = Math.cos(obj_rot);
	let sm  = Math.sin(obj_rot);
	let c   = Math.cos(cam_tilt);
	let s   = Math.sin(cam_tilt);
	let lcm = Math.cos(light_rot);
	let lsm = Math.sin(light_rot);
	let lc  = Math.cos(light_tilt);
	let ls  = Math.sin(light_tilt);
	let lcp = lc*(sm*lsm + cm*lcm);
	return [ lc*(cm*lsm - sm*lcm), s*lcp - c*ls, c*lcp + s*ls ];
}

function MatrixLight( rot, tilt )
{
	let cm = Math.cos(rot);
	let sm = Math.sin(rot);
	let c  = Math.cos(tilt);
	let s  = Math.sin(tilt);
	let r = lightScale;
	return [r*cm,r*s*sm,-r*c*sm, 0,r*c,r*s, -r*sm,r*s*cm,-r*c*cm];
}

function MatrixMVPLight( rot, tilt )
{
	let m = MatrixLight(rot,tilt);
	return [ m[0],m[1],m[2],0, m[3],m[4],m[5],0, m[6],m[7],m[8],0, 0,0,0,1 ];
}

//----------------------------------------------------------------------

function ResizeClumpTexture( res )
{
	// resize clump textures
	clumpRes = parseInt(res);
	gl.activeTexture(gl.TEXTURE9);
	gl.bindTexture(gl.TEXTURE_2D, clumpExtTex);
	gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, clumpRes, clumpRes, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
	gl.bindTexture(gl.TEXTURE_2D, clumpTex);
	gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, clumpRes, clumpRes, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);

	RenderClumpTexture();
}

function UpdateClumpTexture()
{
	SetProgUniform( [ prog_hair, prog_shad_hair, prog_clump ], 'clumpFreq',  clumpFreq );
	SetProgUniform( [ prog_hair, prog_shad_hair ],             'clumpNoise', clumpNoise );
	RenderClumpTexture();
}

function RenderClumpTexture()
{
	SetProgUniform( [ prog_clump ], 'noiseScale', clumpNoise / clumpRes );

	gl.activeTexture(gl.TEXTURE9);
	gl.bindTexture(gl.TEXTURE_2D, clumpTex);
	
	// render clump texture
	gl.bindFramebuffer(gl.FRAMEBUFFER, clumpFramebuffer);
	gl.viewport(0, 0, clumpRes, clumpRes);
	gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, clumpTex, 0);
	gl.clear( gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT );

	gl.bindVertexArray(vao_clump);
	gl.useProgram( prog_clump );
	for ( let b=0; b<hairmesh_bundleLayout.length; ++b ) {
		gl.uniform2f( u_clump_st, hairmesh_bundleLayout[b][0]+0.5, hairmesh_bundleLayout[b][1]+0.5 );
		//gl.drawArrays( gl.TRIANGLE_STRIP, 0, num_quad_verts );
		gl.drawElements( gl.TRIANGLE_STRIP, num_quad_indices, gl.UNSIGNED_SHORT, 0 );
	}
	
	// render extended clump texture
	gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, clumpExtTex, 0);
	gl.clear( gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT );

	gl.bindVertexArray(vao_tex);
	gl.useProgram( prog_clumpExt );
	gl.drawArrays( gl.TRIANGLE_STRIP, 0, 4 );

	gl.bindTexture(gl.TEXTURE_2D, clumpExtTex);
}

//----------------------------------------------------------------------

function DrawTexture()
{
	function GetActiveTexture( unit )
	{
		gl.activeTexture(gl.TEXTURE0 + unit);
		let currentTex = gl.getParameter( gl.TEXTURE_BINDING_2D );
		gl.activeTexture(gl.TEXTURE0);
		return currentTex;
	}
	
	gl.activeTexture(gl.TEXTURE0);
	let currentTex = gl.getParameter( gl.TEXTURE_BINDING_2D );
	
	gl.bindFramebuffer(gl.FRAMEBUFFER, null);
	gl.viewport(0, 0, gl.canvas.width, gl.canvas.height);
	gl.clear( gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT );

	gl.bindVertexArray(vao_tex);
	gl.useProgram( prog_tex );
	
	switch ( showTexture ) {
		case 'clump':    gl.bindTexture(gl.TEXTURE_2D, clumpTex   ); break;
		case 'clumpExt': gl.bindTexture(gl.TEXTURE_2D, clumpExtTex); break;
		case 'shadow':   gl.bindTexture(gl.TEXTURE_2D, GetActiveTexture(3)); break;
	}

	gl.drawArrays( gl.TRIANGLE_STRIP, 0, 4 );
	
	gl.bindTexture(gl.TEXTURE_2D, currentTex );
}

//----------------------------------------------------------------------

function DrawHair( u_st )
{
	switch ( renderMode ) {
	case 0:
		for ( let b=0; b<hairmesh_bundleLayout.length; ++b ) {
			gl.uniform2f( u_st, hairmesh_bundleLayout[b][0]+0.5, hairmesh_bundleLayout[b][1]+0.5 );
			gl.drawArraysInstanced( gl.LINE_STRIP, 0, numHairVerts, numHair/hairmesh_bundleLayout.length );
		}
		break;
	case 1:
		for ( let b=0; b<hairmesh_bundleLayout.length; ++b ) {
			gl.uniform2f( u_st, hairmesh_bundleLayout[b][0]+0.5, hairmesh_bundleLayout[b][1]+0.5 );
			gl.drawArraysInstanced( gl.TRIANGLE_STRIP, 0, numHairVerts*2, numHair/hairmesh_bundleLayout.length );
		}
		break;
	case 2:
		//for ( let b=0; b<hairmesh_bundleLayout.length; ++b ) {
		//	gl.uniform4f( u_st, hairmesh_bundleLayout[b][0]+0.5, hairmesh_bundleLayout[b][1]+0.5, 0, 1/numStrips );
		//	gl.drawArraysInstanced( gl.TRIANGLE_STRIP, 0, numHairVerts*2, numStrips );
		//}
		for ( let i=0; i<numStrips; ++i ) {
			let b = root_samples[i].bundID;
			let s = root_samples[i].s;
			let t = root_samples[i].t;
			//s = Math.random();
			//t = Math.random();
			gl.uniform4f( u_st, hairmesh_bundleLayout[b][0]+0.5, hairmesh_bundleLayout[b][1]+0.5, s, t );
			gl.drawArrays( gl.TRIANGLE_STRIP, 0, numHairVerts*2 );
		}
		break;
	}
}

//----------------------------------------------------------------------

function RenderScene()
{
	let lightMVP = MatrixMVPLight(lightRot+objRot,lightTilt);
	let camMVP = showLightView ? lightMVP : MatrixMVP(objRot,camTilt,camDist);
	let camMV  = showLightView ? lightMVP : MatrixMV (objRot,camTilt,camDist);
	let mLight = MatrixLight(lightRot+objRot,lightTilt);
	let lightDir = showLightView ? [0,0,-1] : MVLightDir( objRot, camTilt, lightRot+objRot, lightTilt );
	
	// Render obj shadows
	gl.bindFramebuffer(gl.FRAMEBUFFER, objDepthFramebuffer);
	gl.viewport(0, 0, depthTextureSize, depthTextureSize);
	gl.clear( gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT );
	if ( showModel ) {
		gl.bindVertexArray(vao_obj);
		gl.useProgram( prog_shad_obj );
		gl.uniformMatrix4fv( u_shad_obj_mvp, false, lightMVP );
		//gl.drawArrays( gl.TRIANGLE_STRIP, 0, num_obj_verts );
		gl.drawElements( gl.TRIANGLE_STRIP, num_obj_verts, gl.UNSIGNED_SHORT, 0 );
	}

	// Render hair shadows
	gl.bindFramebuffer(gl.FRAMEBUFFER, hairDepthFramebuffer);
	gl.viewport(0, 0, depthTextureSize, depthTextureSize);
	gl.clear( gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT );
	if ( showHair ) {
		gl.bindVertexArray(vao_hair);
		gl.useProgram( prog_shad_hair );
		gl.uniformMatrix4fv( u_shad_hair_mvp, false, lightMVP );
		gl.uniformMatrix4fv( u_shad_hair_mv,  false, lightMVP );
		DrawHair(u_shad_hair_st);
	}

	if ( ! showTexture == '' ) return DrawTexture();

	// Render camera view
	gl.bindFramebuffer(gl.FRAMEBUFFER, null);
	gl.viewport(0, 0, gl.canvas.width, gl.canvas.height);
	gl.clear( gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT );

	if ( showModel ) {
		gl.bindVertexArray(vao_obj);
		gl.useProgram( prog_obj );
		gl.uniformMatrix4fv( u_obj_mvp,    false, camMVP );
		gl.uniformMatrix4fv( u_obj_mv,     false, camMV  );
		gl.uniformMatrix3fv( u_obj_mLight, false, mLight );
		gl.uniform3fv( u_obj_lightDir, lightDir );
		//gl.drawArrays( gl.TRIANGLE_STRIP, 0, num_obj_verts );
		gl.drawElements( gl.TRIANGLE_STRIP, num_obj_verts, gl.UNSIGNED_SHORT, 0 );
	}

	if ( showHair ) {
		gl.bindVertexArray(vao_hair);
		gl.useProgram( prog_hair );
		gl.uniformMatrix4fv( u_hair_mvp,    false, camMVP );
		gl.uniformMatrix4fv( u_hair_mv,     false, camMV  );
		gl.uniformMatrix3fv( u_hair_mLight, false, mLight );
		gl.uniform3fv( u_hair_lightDir, lightDir );
		DrawHair(u_hair_st);
	}
	
	if ( showLightDir ) {
		gl.bindVertexArray(vao_light);
		gl.useProgram( prog_light );
		gl.uniformMatrix4fv( u_light_mvp,    false, camMVP );
		gl.uniformMatrix3fv( u_light_mLight, false, mLight );
		gl.enable( gl.BLEND );
		gl.drawArrays( gl.LINES, 0, num_light_verts );
		gl.disable( gl.BLEND );
	}
	
	if ( showHairMesh ) {
		gl.bindVertexArray(vao_hairmeshC);
		gl.useProgram( prog_hairmeshC );
		gl.uniformMatrix4fv( u_hairmeshC_mvp, false, camMVP );
		gl.drawArraysInstanced( gl.LINE_STRIP, 0, numHairMeshCurveVerts, tex_hairmesh_width*tex_hairmesh_height );

		gl.bindVertexArray(vao_hairmeshL);
		gl.useProgram( prog_hairmeshL );
		gl.uniformMatrix4fv( u_hairmeshL_mvp, false, camMVP );
		gl.drawArraysInstanced( gl.LINES, 0, numHairMeshLineVerts, (tex_hairmesh_depth-1)/3 + 1 );
	}

	//window.requestAnimationFrame(RenderScene);
	//console.log('rendered');
}

//----------------------------------------------------------------------

function LoadTextureImage( url, numComponents, unit, dim=2, defaultVal=[255, 255, 255, 255] )
{
	const type = dim == 2 ? gl.TEXTURE_2D : gl.TEXTURE_3D;
	const textureUnit = gl.TEXTURE0 + unit;
	const texture = gl.createTexture();
	gl.activeTexture( textureUnit );
	gl.bindTexture( type, texture );
	const format = numComponents==3 ? gl.RGB : gl.RGBA;
	if ( dim == 2 ) {
		gl.texImage2D( type, 0, format, 1, 1, 0, format, gl.UNSIGNED_BYTE, new Uint8Array(defaultVal) );
	} else {
		gl.texImage3D( type, 0, format, 1, 1, 1, 0, format, gl.UNSIGNED_BYTE, new Uint8Array(defaultVal) );
	}
	gl.texParameteri(type, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
	gl.texParameteri(type, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
	if ( dim == 3 ) gl.texParameteri(type, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
	gl.texParameteri(type, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
	const image = new Image();
	image.texture = texture;
	image.onload = () => {
		gl.activeTexture( textureUnit );
		gl.bindTexture( type, texture );
		try {
			if ( dim == 2 ) {
				gl.texImage2D( type, 0, format, format, gl.UNSIGNED_BYTE, image );
			} else {
				let w = image.width;
				let h = w;
				let d = image.height/h;
				gl.texImage3D( type, 0, format, w, h, d, 0, format, gl.UNSIGNED_BYTE, image );
			}
		} catch (err) { console.log(err); }
		pendingTextures--;
		if ( pendingTextures <= 0 ) Refresh();
	};
	pendingTextures++;
	image.src = url;
	return image;
}

//----------------------------------------------------------------------

function SetHairMaterial(name)
{
	LoadTextureImage("materials/"+name+"/N_TT_R.png",   4,10,2,[13,11,9,5]);
	LoadTextureImage("materials/"+name+"/N_TRT.png",    3,11,2,[17,13,8]);
	LoadTextureImage("materials/"+name+"/N_TT_R_GI.png",4,12,2,[42,38,31,5]);
	LoadTextureImage("materials/"+name+"/N_TRT_GI.png", 3,13,2,[22,19,13]);
	LoadTextureImage("materials/"+name+"/GI.png",       3,14,3,[34,24,12]);
	LoadTextureImage("materials/"+name+"/M_GI.png",     3,15,2,[29,34,49]);
	document.getElementById('mtlScript').src = 'materials/'+name+'/mtl.js';
}

//----------------------------------------------------------------------

function w2t( w )
{
	let segment = Math.min( maxLevel-1, Math.floor( maxLevel * w ) );
	let t = maxLevel * w - segment;
	let t1 = ( 3* segment    + 0.5 + (1-t)*t ) / tex_hairmesh_depth;
	let t2 = ( 3*(segment+1) + 0.5 - (1-t)*t ) / tex_hairmesh_depth;
	return [ t, t1, t2 ];
}

//----------------------------------------------------------------------

function SetBuffer( prog, name, buffer, dim )
{
	let p = gl.getAttribLocation(prog, name);
	gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
	gl.vertexAttribPointer(p, dim, gl.FLOAT, false, 0, 0);
	gl.enableVertexAttribArray(p);
}

//----------------------------------------------------------------------

function UpdateHairInstance()
{
	let texWscale = maxLevel/(maxLevel+1);
	let texWshift = 0.5/(maxLevel+1);
	let bias = 0.5;
	if ( renderMode == 2 ) bias = 2;

	let hairPos  = Array( numHairVerts*4 );
	let hairPos2 = Array( numHairVerts*4 );
	let hairTexW = Array( numHairVerts*2 );
	for ( let i=0; i<numHairVerts; ++i ) {
		let w = i / (numHairVerts-1);
		w = Math.pow(w,bias);
		let t = w2t(w);
		hairPos[4*i  ] = t[1];
		hairPos[4*i+1] = t[2];
		hairPos[4*i+2] = t[0];
		hairPos[4*i+3] = w;
		let di = w<0.5?1:-1;
		let w2 = (i + di) / (numHairVerts-1);
		w2 = Math.pow(w2,bias);
		let tt = w2t(w2);
		hairPos2[4*i  ] = tt[1];
		hairPos2[4*i+1] = tt[2];
		hairPos2[4*i+2] = tt[0];
		hairPos2[4*i+3] = di/(numHairVerts-1);
		hairTexW[2*i  ] = w  * texWscale + texWshift;
		hairTexW[2*i+1] = w2 * texWscale + texWshift;
	}
	
	if ( renderMode > 0 ) {
		let w = renderMode == 1 ? hairWidth : stripWidth;
		let width = w / resolution;
		let tHairPos  = Array( numHairVerts*8 );
		let tHairPos2 = Array( numHairVerts*8 );
		let tHairTexW = Array( numHairVerts*6 );
		for ( let i=0; i<numHairVerts; ++i ) {
			tHairPos[8*i  ] = hairPos[4*i  ];
			tHairPos[8*i+1] = hairPos[4*i+1];
			tHairPos[8*i+2] = hairPos[4*i+2];
			tHairPos[8*i+3] = hairPos[4*i+3];
			tHairPos[8*i+4] = hairPos[4*i  ];
			tHairPos[8*i+5] = hairPos[4*i+1];
			tHairPos[8*i+6] = hairPos[4*i+2];
			tHairPos[8*i+7] = hairPos[4*i+3];
			tHairPos2[8*i  ] = hairPos2[4*i  ];
			tHairPos2[8*i+1] = hairPos2[4*i+1];
			tHairPos2[8*i+2] = hairPos2[4*i+2];
			tHairPos2[8*i+3] = hairPos2[4*i+3];
			tHairPos2[8*i+4] = hairPos2[4*i  ];
			tHairPos2[8*i+5] = hairPos2[4*i+1];
			tHairPos2[8*i+6] = hairPos2[4*i+2];
			tHairPos2[8*i+7] = hairPos2[4*i+3];
			tHairTexW[6*i  ] = hairTexW[2*i  ];
			tHairTexW[6*i+1] = hairTexW[2*i+1];
			tHairTexW[6*i+2] = -width;
			tHairTexW[6*i+3] = hairTexW[2*i  ];
			tHairTexW[6*i+4] = hairTexW[2*i+1];
			tHairTexW[6*i+5] = width;
		}
		hairPos  = tHairPos;
		hairPos2 = tHairPos2;
		hairTexW = tHairTexW;
	}
	
	function UpdateBuffer( buffer, data )
	{
		gl.bindBuffer( gl.ARRAY_BUFFER, buffer );
		gl.bufferData( gl.ARRAY_BUFFER, new Float32Array(data), gl.STATIC_DRAW );
	}
	
	UpdateBuffer( hair_buffer_pos,  hairPos  );
	UpdateBuffer( hair_buffer_pos2, hairPos2 );
	UpdateBuffer( hair_buffer_texW, hairTexW );
	
	gl.bindVertexArray(vao_hair);
	SetBuffer( prog_hair, 'pos',  hair_buffer_pos,  4 );
	SetBuffer( prog_hair, 'pos2', hair_buffer_pos2, 4 );
	SetBuffer( prog_hair, 'texW', hair_buffer_texW, renderMode == 0 ? 2 : 3 );

}

//----------------------------------------------------------------------

function ReportGLSLError( errorStr, source )
{
	let lines       = errorStr.split(/\r?\n|\r|\n/g);
	let sourceLines = source  .split(/\r?\n|\r|\n/g);
	lines.forEach( (line) => {
		console.log(line);
		if ( line.substr(0,7) == 'ERROR: ' ) {
			let lineNo = line.split(':')[2];
			console.log('Line ' + lineNo + ': ' + sourceLines[lineNo-1]);
		}
	});
}

function CompileShader( type, source )
{
	const shader = gl.createShader(type);
	gl.shaderSource(shader, source);
	gl.compileShader(shader);
	if ( ! gl.getShaderParameter(shader, gl.COMPILE_STATUS) ) {
		console.log( "GLSL shader error:" );
		ReportGLSLError( gl.getShaderInfoLog(shader), source );
		gl.deleteShader(shader);
	}
	return shader;
}

function CompileShaders( vs_source, fs_source )
{
	const vs = CompileShader( gl.VERTEX_SHADER,   vs_source );
	const fs = CompileShader( gl.FRAGMENT_SHADER, fs_source );
	const prog = gl.createProgram();
	gl.attachShader(prog, vs);
	gl.attachShader(prog, fs);
	gl.linkProgram(prog);
	if ( ! gl.getProgramParameter(prog, gl.LINK_STATUS) ) {
		console.log( "GLSL program error:\n" + gl.getProgramInfoLog(prog) );
	}
	gl.deleteShader(vs);
	gl.deleteShader(fs);
	return prog;
}

//----------------------------------------------------------------------

function CompileHairShaders()
{
	let vs_hair_source = vs_hair_line_source;
	switch ( renderMode ) {
		case 0: vs_hair_source = vs_hair_line_source;  break;
		case 1: vs_hair_source = vs_hair_tri_source;   break;
		case 2: vs_hair_source = vs_hair_strip_source; break;
	}
	
	let p_hair      = CompileShaders( vs_hair_source,      fs_hair_source        );
	let p_shad_hair = CompileShaders( vs_hair_source,      fs_shadow_hair_source );
	
	u_hair_mvp      = gl.getUniformLocation( p_hair,      'mvp'      );
	u_hair_mv       = gl.getUniformLocation( p_hair,      'mv'       );
	u_hair_mLight   = gl.getUniformLocation( p_hair,      'mLight'   );
	u_hair_lightDir = gl.getUniformLocation( p_hair,      'lightDir' );
	u_hair_st       = gl.getUniformLocation( p_hair,      'st'       );
	u_shad_hair_mvp = gl.getUniformLocation( p_shad_hair, 'mvp'      );
	u_shad_hair_mv  = gl.getUniformLocation( p_shad_hair, 'mv'       );
	u_shad_hair_st  = gl.getUniformLocation( p_shad_hair, 'st'       );

	gl.useProgram( p_hair );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'objShadMap'   ),  2 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'hairShadMap'  ),  3 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'tex_hairmesh' ),  4 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'tex_uv'       ),  5 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'tex_uw'       ),  6 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'tex_v'        ),  7 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'tex_noise'    ),  8 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'tex_clump'    ),  9 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'N_TT_R'       ), 10 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'N_TRT'        ), 11 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'N_TT_R_GI'    ), 12 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'N_TRT_GI'     ), 13 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'GI'           ), 14 );
	gl.uniform1i( gl.getUniformLocation( p_hair, 'M_GI'         ), 15 );

	gl.useProgram( p_shad_hair );
	gl.uniform1i( gl.getUniformLocation( p_shad_hair, 'tex_hairmesh' ), 4 );
	gl.uniform1i( gl.getUniformLocation( p_shad_hair, 'tex_uv'       ), 5 );
	gl.uniform1i( gl.getUniformLocation( p_shad_hair, 'tex_uw'       ), 6 );
	gl.uniform1i( gl.getUniformLocation( p_shad_hair, 'tex_v'        ), 7 );
	gl.uniform1i( gl.getUniformLocation( p_shad_hair, 'tex_noise'    ), 8 );
	gl.uniform1i( gl.getUniformLocation( p_shad_hair, 'tex_clump'    ), 9 );
	
	if ( typeof prog_hair !== 'undefined' ) {
		gl.deleteProgram(prog_hair);
		gl.deleteProgram(prog_shad_hair);
		let old_prog_shad_hair = prog_shad_hair;
		prog_hair      = p_hair;
		prog_shad_hair = p_shad_hair;
		hairParam.forEach( function(cInput) {
			cInput.updateProgs[0] = prog_hair;
			if ( cInput.updateProgs[1] == old_prog_shad_hair ) cInput.updateProgs[1] = prog_shad_hair;
			cInput.update();
		});
	} else {
		prog_hair      = p_hair;
		prog_shad_hair = p_shad_hair;
	}
}

//----------------------------------------------------------------------

function InitRendering()
{
	canvas = document.getElementById( "glcanvas" );
	canvas.addEventListener( "touchstart",  touchDown );
	canvas.addEventListener( "touchend",    touchUp   );
	canvas.addEventListener( "touchcancel", touchUp   );
	canvas.addEventListener( "touchmove",   touchMove );
	
	// Initialize WebGL
	gl = canvas.getContext("webgl2", {antialias: true} );
	console.log(gl.getParameter(gl.VERSION));

	// Set the output resolution and viewport
	// We can change the output resolution later on.
	// This is helpful, for example, when the user changes the size of the window.
	//const pixelRatio = window.devicePixelRatio || 1;
	//const pixelRatio = 1;
	//canvas.width  = pixelRatio * canvas.clientWidth;
	//canvas.height = pixelRatio * canvas.clientHeight;
	canvas.width  = resolution;
	canvas.height = resolution;
	gl.viewport(0, 0, canvas.width, canvas.height);

	// Initialize other WebGL states
	gl.clearColor(0, 0, 0, 0);
	gl.enable( gl.DEPTH_TEST );
	gl.blendFunc( gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA );
		
	///////////////////////////////////////////////////////////////////////
	// Prepare shadow maps
	
	function generateDepthRender( unit )
	{
		let depthTex = gl.createTexture();
		gl.activeTexture(gl.TEXTURE0 + unit);
		gl.bindTexture  (gl.TEXTURE_2D, depthTex);
		gl.texStorage2D (gl.TEXTURE_2D, 1, gl.DEPTH_COMPONENT24, depthTextureSize, depthTextureSize);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

		let framebuffer = gl.createFramebuffer();
		gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
		gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, depthTex, 0);
		return framebuffer;
	}
	
	objDepthFramebuffer = generateDepthRender(2);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_MODE, gl.COMPARE_REF_TO_TEXTURE);

	hairDepthFramebuffer = generateDepthRender(3);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
	

	///////////////////////////////////////////////////////////////////////
	// Compile the shaders
	
	CompileHairShaders();
	
	prog_obj       = CompileShaders( vs_obj_source,       fs_obj_source         );
	prog_light     = CompileShaders( vs_light_source,     fs_light_source       );
	prog_clump     = CompileShaders( vs_clump_source,     fs_clump_source       );
	prog_clumpExt  = CompileShaders( vs_tex_source,       fs_clumpExt_source    );
	prog_hairmeshC = CompileShaders( vs_hairmeshC_source, fs_hairmesh_source    );
	prog_hairmeshL = CompileShaders( vs_hairmeshL_source, fs_hairmesh_source    );
	prog_shad_obj  = CompileShaders( vs_obj_source,       fs_shadow_source      );
	prog_tex       = CompileShaders( vs_tex_source,       fs_tex_source         );
	
	u_obj_mvp       = gl.getUniformLocation( prog_obj,       'mvp'      );
	u_obj_mv        = gl.getUniformLocation( prog_obj,       'mv'       );
	u_obj_mLight    = gl.getUniformLocation( prog_obj,       'mLight'   );
	u_obj_lightDir  = gl.getUniformLocation( prog_obj,       'lightDir' );
	u_clump_st      = gl.getUniformLocation( prog_clump,     'st'       );
	u_hairmeshC_mvp = gl.getUniformLocation( prog_hairmeshC, 'mvp'      );
	u_hairmeshL_mvp = gl.getUniformLocation( prog_hairmeshL, 'mvp'      );
	u_shad_obj_mvp  = gl.getUniformLocation( prog_shad_obj,  'mvp'      );
	u_light_mvp     = gl.getUniformLocation( prog_light,     'mvp'      );
	u_light_mLight  = gl.getUniformLocation( prog_light,     'mLight'   );
	
	gl.useProgram( prog_obj );
	gl.uniform1i( gl.getUniformLocation( prog_obj,  'objShadMap'   ),  2 );
	gl.uniform1i( gl.getUniformLocation( prog_obj,  'hairShadMap'  ),  3 );


	gl.useProgram( prog_clump );
	gl.uniform1i( gl.getUniformLocation( prog_clump, 'tex_uv'      ), 5 );
	gl.uniform1i( gl.getUniformLocation( prog_clump, 'tex_noise'   ), 8 );

	gl.useProgram( prog_clumpExt );
	gl.uniform1i( gl.getUniformLocation( prog_clumpExt, 'tex_clump'),  9 );

	gl.useProgram( prog_hairmeshC );
	gl.uniform1i( gl.getUniformLocation( prog_hairmeshC, 'tex_hairmesh' ), 4 );
	gl.useProgram( prog_hairmeshL );
	gl.uniform1i( gl.getUniformLocation( prog_hairmeshL, 'tex_hairmesh' ), 4 );

	///////////////////////////////////////////////////////////////////////
	// scale and center the positions
	
	const big = 99999;
	let pos_min = [  big,  big,  big ];
	let pos_max = [ -big, -big, -big ];
	for ( let i=0; i<obj_vert_pos.length; i+=3 ) {
		for ( let j=0; j<3; ++j ) {
			let p = obj_vert_pos[i+j];
			pos_min[j] = Math.min( p, pos_min[j] );
			pos_max[j] = Math.max( p, pos_max[j] );
		}
	}
	for ( let i=0; i<hairmesh_verts.length; ++i ) {
		let p = hairmesh_verts[i].p;
		for ( let j=0; j<3; ++j ) {
			pos_min[j] = Math.min( p[j], pos_min[j] );
			pos_max[j] = Math.max( p[j], pos_max[j] );
		}
	}

	let center = [
		(pos_max[0]+pos_min[0])/2,
		(pos_max[1]+pos_min[1])/2,
		(pos_max[2]+pos_min[2])/2,
	];
	let sizeX = pos_max[0] - pos_min[0];
	let sizeY = pos_max[1] - pos_min[1];
	let sizeZ = pos_max[2] - pos_min[2];
	let scale = 2 / Math.max( sizeX, sizeY, sizeZ );
	
	for ( let i=0; i<obj_vert_pos.length; i+=3 ) {
		for ( let j=0; j<3; ++j ) {
			obj_vert_pos[i+j] -= center[j];
			obj_vert_pos[i+j] *= scale;
			obj_vert_pos[i+j] += modelShift[j];
		}
	}
	for ( let i=0; i<hairmesh_verts.length; ++i ) {
		for ( let j=0; j<3; ++j ) {
			hairmesh_verts[i].p[j] -= center[j];
			hairmesh_verts[i].p[j] *= scale;
			hairmesh_verts[i].p[j] += modelShift[j];
		}
	}
	
	for ( let i=0; i<obj_colliders.length; ++i ) {
		for ( let j=0; j<3; ++j ) {
			obj_colliders[i][j] -= center[j];
			obj_colliders[i][j] *= scale;
			obj_colliders[i][j] += modelShift[j];
			if ( obj_colliders[i].length > 4 ) {
				obj_colliders[i][j+4] -= center[j];
				obj_colliders[i][j+4] *= scale;
				obj_colliders[i][j+4] += modelShift[j];
			}
		}
		obj_colliders[i][3] *= scale;
		if ( obj_colliders[i].length > 4 ) {
			obj_colliders[i][7] *= scale;
		}
	}

	///////////////////////////////////////////////////////////////////////
	// Create: hair mesh texture

	function createHMTex( unit, dim ) {
		const tex = gl.createTexture();
		gl.activeTexture( gl.TEXTURE0 + unit );
		let target = dim == 3 ? gl.TEXTURE_3D : gl.TEXTURE_2D;
		gl.bindTexture  ( target, tex);
		gl.texParameteri( target, gl.TEXTURE_MIN_FILTER, gl.LINEAR );
		gl.texParameteri( target, gl.TEXTURE_MAG_FILTER, gl.LINEAR );
		gl.texParameteri( target, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE );
		gl.texParameteri( target, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE );
		if ( dim == 3 ) gl.texParameteri( target, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE );
		return tex;
	}
	
	// hair mesh texture depth
	maxLevel = 0;
	let hairMeshVertLevels = new Array( hairmesh_verts.length );
	for ( let i=0; i<hairmesh_bundles.length; ++i ) {
		let level = -1;
		let b = hairmesh_bundles[i];
		let v = b[0];
		while ( v >= 0 ) {
			v = hairmesh_verts[v].prev;
			level++;
		}
		if ( maxLevel < level ) maxLevel = level;
		for ( let j=0; j<b.length; ++j ) {
			let v = b[j];
			for ( let l=level; l>=0; --l ) {
				hairMeshVertLevels[v] = l;
				v = hairmesh_verts[v].prev;
			}
		}
	}
	tex_hairmesh_depth = 3*maxLevel + 1;
	
	function GenerateBundleLayout( bundles ) {
		// generate bundle layout
		let layout = new Array();
		for ( let i=0; i<hairmesh_bundleLayout.length; ++i ) {
			let b = bundles[i];
			let l = hairmesh_bundleLayout[i];
			if ( !Array.isArray( layout[ l[1]   ] ) ) layout[ l[1]   ] = new Array();
			if ( !Array.isArray( layout[ l[1]+1 ] ) ) layout[ l[1]+1 ] = new Array();
			layout[ l[1]   ][ l[0]   ] = b[0];
			layout[ l[1]   ][ l[0]+1 ] = b[1];
			layout[ l[1]+1 ][ l[0]+1 ] = b[2];
			layout[ l[1]+1 ][ l[0]   ] = b[3];
		}
		
		// verify the layout
		for ( let i=0; i<hairmesh_bundleLayout.length; ++i ) {
			let b = bundles[i];
			let l = hairmesh_bundleLayout[i];
			if ( ( layout[ l[1]   ][ l[0]   ] != b[0] ) ||
				 ( layout[ l[1]   ][ l[0]+1 ] != b[1] ) ||
				 ( layout[ l[1]+1 ][ l[0]+1 ] != b[2] ) ||
				 ( layout[ l[1]+1 ][ l[0]   ] != b[3] ) )
			{
				console.log('Hair mesh layout error: [' +
					layout[ l[1]   ][ l[0]   ] + ', ' +
					layout[ l[1]   ][ l[0]+1 ] + ', ' +
					layout[ l[1]+1 ][ l[0]+1 ] + ', ' +
					layout[ l[1]+1 ][ l[0]   ] + '] != [' +
					b[0] + ', ' + b[1] + ', ' + b[2] + ', ' + b[3] + ']');
			}
		}
		return layout;
	}
	
	let hairMeshTextureLayout = GenerateBundleLayout( hairmesh_bundles );
	
	// hair mesh texture width and height
	tex_hairmesh_height = hairMeshTextureLayout.length;
	tex_hairmesh_width = 0;
	for ( let i=0; i<tex_hairmesh_height; ++i ) {
		if ( tex_hairmesh_width < hairMeshTextureLayout[i].length ) tex_hairmesh_width = hairMeshTextureLayout[i].length;
	}
	
	function plen( p0, p1 )
	{
		let p01 = [ p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] ];
		return Math.sqrt( p01[0]*p01[0] + p01[1]*p01[1] + p01[2]*p01[2] );
	}
	
	tex_hairmesh_ids     = new Array( tex_hairmesh_width * tex_hairmesh_height * tex_hairmesh_depth * 3 );
	tex_hairmesh_weights = new Array( tex_hairmesh_width * tex_hairmesh_height * tex_hairmesh_depth * 3 );
	for ( let i=0; i<tex_hairmesh_ids.length; ++i ) tex_hairmesh_ids[i] = 0;
	for ( let i=0; i<tex_hairmesh_weights.length; ++i ) tex_hairmesh_weights[i] = 0;
	for ( let y=0; y<tex_hairmesh_height; ++y ) {
		for ( let x=0; x<hairMeshTextureLayout[y].length; ++x ) {
			let v1 = hairMeshTextureLayout[y][x];
			let level = hairMeshVertLevels[v1];
			let v0 = v1;
			for ( let lev=level; lev>0; --lev ) {
				let p1 = hairmesh_verts[v1].p;
				let v2 = hairmesh_verts[v1].prev;
				let p2 = hairmesh_verts[v2].p;
				let l2 = plen( p1, p2 );
				let s1i0 = v0;
				let s2i0 = v0;
				let s1w0 = 1;
				let s1w1 = 0;
				let s1w2 = 0;
				let s2w0 = 1;
				let s2w1 = 0;
				let s2w2 = 0;
				if ( v0 != v1 ) {
					let p0 = hairmesh_verts[v0].p;
					let l1 = plen( p0, p1 );
					let L = l1 + Math.sqrt(l1*l2);
					s1i0 = v0;
					s1w0 = -l2/L;
					s1w1 = (l2-l1)/L + 2;
					s1w2 = l1/L - 1;
					if ( lev > 1 ) {
						let v3 = hairmesh_verts[v2].prev;
						let p3 = hairmesh_verts[v3].p;
						let l3 = plen( p3, p2 );
						let L = l3 + Math.sqrt(l3*l2);
						s2i0 = v3;
						s2w0 = -l2/L;
						s2w1 = (l2-l3)/L + 2;
						s2w2 = l3/L - 1;
					} else {
						s2w0 = -s1w0;
						s2w1 =  s1w1;
						s2w2 =  s1w2;
					}
				} else {
					if ( lev > 1 ) {
						let v3 = hairmesh_verts[v2].prev;
						let p3 = hairmesh_verts[v3].p;
						let l3 = plen( p3, p2 );
						let L = l3 + Math.sqrt(l3*l2);
						s1i0 = v3;
						s2i0 = v3;
						s1w0 = l2/L;
						s1w1 = (l2-l3)/L+2;
						s1w2 = l3/L-1;
						s2w0 = -s1w0;
						s2w1 =  s1w1;
						s2w2 =  s1w2;
					} else {
						s1i0 = v1;
						s2i0 = v2;
					}
				}
				let z = 3*lev;
				let i = ( z*tex_hairmesh_height + y ) * tex_hairmesh_width + x;
				let dz = tex_hairmesh_width * tex_hairmesh_height;
				tex_hairmesh_ids    [ 3*i     ] = v1;
				tex_hairmesh_ids    [ 3*i + 1 ] = 0;
				tex_hairmesh_ids    [ 3*i + 2 ] = 0;
				tex_hairmesh_ids    [ 3*(i-  dz)     ] = s1i0;
				tex_hairmesh_ids    [ 3*(i-  dz) + 1 ] = v1;
				tex_hairmesh_ids    [ 3*(i-  dz) + 2 ] = v2;
				tex_hairmesh_ids    [ 3*(i-2*dz)     ] = s2i0;
				tex_hairmesh_ids    [ 3*(i-2*dz) + 1 ] = v2;
				tex_hairmesh_ids    [ 3*(i-2*dz) + 2 ] = v1;
				tex_hairmesh_weights[ 3*i     ] = 1;
				tex_hairmesh_weights[ 3*i + 1 ] = 0;
				tex_hairmesh_weights[ 3*i + 2 ] = 0;
				tex_hairmesh_weights[ 3*(i-  dz)     ] = s1w0;
				tex_hairmesh_weights[ 3*(i-  dz) + 1 ] = s1w1;
				tex_hairmesh_weights[ 3*(i-  dz) + 2 ] = s1w2;
				tex_hairmesh_weights[ 3*(i-2*dz)     ] = s2w0;
				tex_hairmesh_weights[ 3*(i-2*dz) + 1 ] = s2w1;
				tex_hairmesh_weights[ 3*(i-2*dz) + 2 ] = s2w2;
				v0 = v1;
				v1 = v2;
			}
			let i = y*tex_hairmesh_width + x;
			tex_hairmesh_ids    [ 3*i     ] = v1;
			tex_hairmesh_ids    [ 3*i + 1 ] = 0;
			tex_hairmesh_ids    [ 3*i + 2 ] = 0;
			tex_hairmesh_weights[ 3*i     ] = 1;
			tex_hairmesh_weights[ 3*i + 1 ] = 0;
			tex_hairmesh_weights[ 3*i + 2 ] = 0;
		}
	}
	
	createHMTex( 4, 3 );
	
	///////////////////////////////////////////////////////////////////////
	// Create: uv-texture

	let uvTextureLayout = GenerateBundleLayout( hairmesh_bundleTexCoords );
	
	let tex_uv_data = new Array( tex_hairmesh_width * tex_hairmesh_height * 2 );
	for ( let y=0; y<tex_hairmesh_height; ++y ) {
		for ( let x=0; x<uvTextureLayout[y].length; ++x ) {
			tex_uv_data[ 2*(y*tex_hairmesh_width + x)     ] = hairmesh_texCoords[ uvTextureLayout[y][x] ][0];
			tex_uv_data[ 2*(y*tex_hairmesh_width + x) + 1 ] = hairmesh_texCoords[ uvTextureLayout[y][x] ][1];
		}
	}

	createHMTex( 5, 2 );
	gl.texImage2D( gl.TEXTURE_2D, 0, gl.RG16F, tex_hairmesh_width, tex_hairmesh_height, 0, gl.RG, gl.FLOAT, new Float32Array(tex_uv_data) );
	
	///////////////////////////////////////////////////////////////////////
	// Create: u-texture and v-texture

	let tex_vertTri = new Array( hairmesh_verts.length );
	function buildTMatrix( tc0, tc1, tc2 ) {
		let m0 = tc1[0] - tc0[0];
		let m1 = tc1[1] - tc0[1];
		let m2 = tc2[0] - tc0[0];
		let m3 = tc2[1] - tc0[1];
		return [ m3, -m1, -m2, m0 ];
	}
	function setNewTri( v, vn, vp, m ) {
		if ( ! Array.isArray( tex_vertTri[v] ) ) tex_vertTri[v] = new Array();
		tex_vertTri[v].push( { v1:vn, v2:vp, m:m } );
	}
	for ( let i=0; i<hairmesh_bundles.length; ++i ) {
		let b = hairmesh_bundles[i];
		let bt = hairmesh_bundleTexCoords[i];
		let tc0 = hairmesh_texCoords[ bt[0] ];
		let tc1 = hairmesh_texCoords[ bt[1] ];
		let tc2 = hairmesh_texCoords[ bt[2] ];
		if ( b.length == 4 ) {
			let tc3 = hairmesh_texCoords[ bt[3] ];
			let m0 = buildTMatrix( tc0, tc1, tc3 );
			let m1 = buildTMatrix( tc1, tc2, tc0 );
			let m2 = buildTMatrix( tc2, tc3, tc1 );
			let m3 = buildTMatrix( tc3, tc0, tc2 );
			let v = [ b[0], b[1], b[2], b[3] ];
			while ( v[0] >= 0 ) {
				setNewTri( v[0], v[1], v[3], m0 );
				setNewTri( v[1], v[2], v[0], m1 );
				setNewTri( v[2], v[3], v[1], m2 );
				setNewTri( v[3], v[0], v[2], m3 );
				v[0] = hairmesh_verts[v[0]].prev;
				v[1] = hairmesh_verts[v[1]].prev;
				v[2] = hairmesh_verts[v[2]].prev;
				v[3] = hairmesh_verts[v[3]].prev;
			}
		} else {
			let m0 = buildTMatrix( tc0, tc1, tc2 );
			let m1 = buildTMatrix( tc1, tc2, tc0 );
			let m2 = buildTMatrix( tc2, tc0, tc1 );
			let v = [ b[0], b[1], b[2] ];
			while ( v[0] >= 0 ) {
				setNewTri( v[0], v[1], v[2], m0 );
				setNewTri( v[1], v[2], v[0], m1 );
				setNewTri( v[2], v[0], v[1], m2 );
				v[0] = hairmesh_verts[v[0]].prev;
				v[1] = hairmesh_verts[v[1]].prev;
				v[2] = hairmesh_verts[v[2]].prev;
			}
		}
	}
	
	tex_uvw_ids = new Array( tex_hairmesh_width * tex_hairmesh_height * (maxLevel+1) );
	for ( let i=0; i<tex_uvw_ids.length; ++i ) tex_uvw_ids[i] = -1;
	for ( let y=0; y<tex_hairmesh_height; ++y ) {
		for ( let x=0; x<hairMeshTextureLayout[y].length; ++x ) {
			let v = hairMeshTextureLayout[y][x];
			let level = hairMeshVertLevels[v];
			for ( let lev=level; lev>=0; --lev ) {
				tex_uvw_ids[ (lev*tex_hairmesh_height + y ) * tex_hairmesh_width + x ] = v;
				v = hairmesh_verts[v].prev;
			}
		}
	}

	createHMTex( 6, 3 );
	createHMTex( 7, 3 );
	
	///////////////////////////////////////////////////////////////////////
	// Update Hair Mesh texture data
	
	let tex_hairmesh_data = new Array( tex_hairmesh_ids.length * 3 );
	for ( let i=0; i<tex_hairmesh_ids.length; i+=3 ) {
		let v0 = tex_hairmesh_ids[ i     ];
		let v1 = tex_hairmesh_ids[ i + 1 ];
		let v2 = tex_hairmesh_ids[ i + 2 ];
		let w0 = tex_hairmesh_weights[ i     ];
		let w1 = tex_hairmesh_weights[ i + 1 ];
		let w2 = tex_hairmesh_weights[ i + 2 ];
		if ( w2 == 0 ) {
			tex_hairmesh_data[i]   = hairmesh_verts[v0].p[0];
			tex_hairmesh_data[i+1] = hairmesh_verts[v0].p[1];
			tex_hairmesh_data[i+2] = hairmesh_verts[v0].p[2];
		} else {
			if ( w0 < 0 ) {
				tex_hairmesh_data[i]   = w0 * hairmesh_verts[v0].p[0];
				tex_hairmesh_data[i+1] = w0 * hairmesh_verts[v0].p[1];
				tex_hairmesh_data[i+2] = w0 * hairmesh_verts[v0].p[2];
			} else {
				let dx = hairmesh_verts[v2].p[0] - hairmesh_verts[v1].p[0];
				let dy = hairmesh_verts[v2].p[1] - hairmesh_verts[v1].p[1];
				let dz = hairmesh_verts[v2].p[2] - hairmesh_verts[v1].p[2];
				let rx = hairmesh_verts[v0].p[0] - hairmesh_verts[v2].p[0];
				let ry = hairmesh_verts[v0].p[1] - hairmesh_verts[v2].p[1];
				let rz = hairmesh_verts[v0].p[2] - hairmesh_verts[v2].p[2];
				let s = 1 + 2*( dx*rx + dy*ry + dz*rz ) / ( dx*dx + dy*dy + dz*dz );
				tex_hairmesh_data[i]   = -w0 * ( hairmesh_verts[v0].p[0] - s*dx );
				tex_hairmesh_data[i+1] = -w0 * ( hairmesh_verts[v0].p[1] - s*dy );
				tex_hairmesh_data[i+2] = -w0 * ( hairmesh_verts[v0].p[2] - s*dz );
			}
			tex_hairmesh_data[i]   += w1 * hairmesh_verts[v1].p[0] + w2 * hairmesh_verts[v2].p[0];
			tex_hairmesh_data[i+1] += w1 * hairmesh_verts[v1].p[1] + w2 * hairmesh_verts[v2].p[1];
			tex_hairmesh_data[i+2] += w1 * hairmesh_verts[v1].p[2] + w2 * hairmesh_verts[v2].p[2];
		}
	}
	
	let tex_uw_data = new Array( tex_hairmesh_width * tex_hairmesh_height * tex_hairmesh_depth * 4 );
	let tex_v_data  = new Array( tex_hairmesh_width * tex_hairmesh_height * tex_hairmesh_depth * 3 );
	function normalizeVec( v ) { let len = Math.sqrt( v[0]*v[0] + v[1]*v[1] + v[2]*v[2] ); v[0]/=len; v[1]/=len; v[2]/=len; }
	for ( let i=0; i<tex_uvw_ids.length; ++i ) {
		if ( tex_uvw_ids[i] >= 0 ) {
			let vid = tex_uvw_ids[i];
			let vertTri = tex_vertTri[ vid ];
			let u = [ 0, 0, 0 ];
			let v = [ 0, 0, 0 ];
			let p0 = hairmesh_verts[ vid ].p;
			for ( let j=0; j<vertTri.length; ++j ) {
				let p1 = hairmesh_verts[ vertTri[j].v1 ].p;
				let p2 = hairmesh_verts[ vertTri[j].v2 ].p;
				for ( let k=0; k<3; ++k ) {
					let p10 = p1[k] - p0[k];
					let p20 = p2[k] - p0[k];
					u[k] += p10*vertTri[j].m[0] + p20*vertTri[j].m[1];
					v[k] += p10*vertTri[j].m[2] + p20*vertTri[j].m[3];
				}
			}
			normalizeVec(u);
			normalizeVec(v);
			tex_uw_data[4*i  ] = u[0];
			tex_uw_data[4*i+1] = u[1];
			tex_uw_data[4*i+2] = u[2];
			tex_uw_data[4*i+3] = hairmesh_verts[vid].w;
			tex_v_data[3*i  ] = v[0];
			tex_v_data[3*i+1] = v[1];
			tex_v_data[3*i+2] = v[2];
		} else {
			tex_uw_data[4*i  ] = 0;
			tex_uw_data[4*i+1] = 0;
			tex_uw_data[4*i+2] = 0;
			tex_uw_data[4*i+3] = 0;
			tex_v_data[3*i  ] = 0;
			tex_v_data[3*i+1] = 0;
			tex_v_data[3*i+2] = 0;
		}
	}

	gl.activeTexture( gl.TEXTURE4 );
	gl.texImage3D( gl.TEXTURE_3D, 0, gl.RGB16F, tex_hairmesh_width, tex_hairmesh_height, tex_hairmesh_depth, 0, gl.RGB, gl.FLOAT, new Float32Array(tex_hairmesh_data) );
	
	gl.activeTexture( gl.TEXTURE6 );
	gl.texImage3D( gl.TEXTURE_3D, 0, gl.RGBA16F, tex_hairmesh_width, tex_hairmesh_height, maxLevel+1, 0, gl.RGBA, gl.FLOAT, new Float32Array(tex_uw_data) );

	gl.activeTexture( gl.TEXTURE7 );
	gl.texImage3D( gl.TEXTURE_3D, 0, gl.RGB16F, tex_hairmesh_width, tex_hairmesh_height, maxLevel+1, 0, gl.RGB, gl.FLOAT, new Float32Array(tex_v_data) );
	

	///////////////////////////////////////////////////////////////////////
	// Initialize the vertex buffer objects

	function CreateArrayBuffer( data )
	{
		let buffer = gl.createBuffer();
		gl.bindBuffer( gl.ARRAY_BUFFER, buffer );
		gl.bufferData( gl.ARRAY_BUFFER, new Float32Array(data), gl.STATIC_DRAW );
		return buffer;
	}

	let obj_buffer_pos = CreateArrayBuffer( obj_vert_pos  );
	let obj_buffer_nrm = CreateArrayBuffer( obj_normals   );
	let obj_buffer_uvs = CreateArrayBuffer( obj_texCoords );

	let obj_buffer_ix = gl.createBuffer();
	gl.bindBuffer( gl.ELEMENT_ARRAY_BUFFER, obj_buffer_ix );
	gl.bufferData( gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(obj_indices), gl.STATIC_DRAW );

	num_obj_verts = obj_indices.length;

	let hmCurvePos  = Array( numHairMeshCurveVerts*4 );
	let texWscale = maxLevel/(maxLevel+1);
	let texWshift = 0.5/(maxLevel+1);
	let bias = 0.5;
	for ( let i=0; i<numHairMeshCurveVerts; ++i ) {
		let w = i / (numHairMeshCurveVerts-1);
		w = Math.pow(w,bias);
		let t = w2t(w);
		hmCurvePos[4*i  ] = t[1];
		hmCurvePos[4*i+1] = t[2];
		hmCurvePos[4*i+2] = t[0];
		hmCurvePos[4*i+3] = w;
	}
	let hmcurve_buffer_pos  = CreateArrayBuffer(hmCurvePos);
		
	numHairMeshLineVerts = hairmesh_bundleLayout.length * 4 * 2;
	let hairmeshLpos = Array( numHairMeshLineVerts * 2 );
	for ( let i=0, j=0; i<hairmesh_bundleLayout.length; ++i ) {
		let p = hairmesh_bundleLayout[i];
		hairmeshLpos[j++] = (p[0] + 0.5) / tex_hairmesh_width;
		hairmeshLpos[j++] = (p[1] + 0.5) / tex_hairmesh_height;
		hairmeshLpos[j++] = (p[0] + 1.5) / tex_hairmesh_width;
		hairmeshLpos[j++] = (p[1] + 0.5) / tex_hairmesh_height;
		hairmeshLpos[j++] = (p[0] + 1.5) / tex_hairmesh_width;
		hairmeshLpos[j++] = (p[1] + 0.5) / tex_hairmesh_height;
		hairmeshLpos[j++] = (p[0] + 1.5) / tex_hairmesh_width;
		hairmeshLpos[j++] = (p[1] + 1.5) / tex_hairmesh_height;
		hairmeshLpos[j++] = (p[0] + 1.5) / tex_hairmesh_width;
		hairmeshLpos[j++] = (p[1] + 1.5) / tex_hairmesh_height;
		hairmeshLpos[j++] = (p[0] + 0.5) / tex_hairmesh_width;
		hairmeshLpos[j++] = (p[1] + 1.5) / tex_hairmesh_height;
		hairmeshLpos[j++] = (p[0] + 0.5) / tex_hairmesh_width;
		hairmeshLpos[j++] = (p[1] + 1.5) / tex_hairmesh_height;
		hairmeshLpos[j++] = (p[0] + 0.5) / tex_hairmesh_width;
		hairmeshLpos[j++] = (p[1] + 0.5) / tex_hairmesh_height;
	}
	let hairmeshL_buffer_pos = CreateArrayBuffer(hairmeshLpos);
	
	let lightVerts = [
		0,0,0,
		0,0,-0.9,
		/*
		 0.0000,  0.1000, -0.9,
		 0.0500,  0.0866, -0.9,
		 0.0500,  0.0866, -0.9,
		 0.0866,  0.0500, -0.9,
		 0.0866,  0.0500, -0.9,
		 0.1000,  0.0000, -0.9,
		 0.1000,  0.0000, -0.9,
		 0.0866, -0.0500, -0.9,
		 0.0866, -0.0500, -0.9,
		 0.0500, -0.0866, -0.9,
		 0.0500, -0.0866, -0.9,
		 0.0000, -0.1000, -0.9,
		 0.0000, -0.1000, -0.9,
		-0.0500, -0.0866, -0.9,
		-0.0500, -0.0866, -0.9,
		-0.0866, -0.0500, -0.9,
		-0.0866, -0.0500, -0.9,
		-0.1000,  0.0000, -0.9,
		-0.1000,  0.0000, -0.9,
		-0.0866,  0.0500, -0.9,
		-0.0866,  0.0500, -0.9,
		-0.0500,  0.0866, -0.9,
		-0.0500,  0.0866, -0.9,
		 0.0000,  0.1000, -0.9,
		*/
		-0.1, -0.1, -0.9,
		 0.1, -0.1, -0.9,
		 0.1, -0.1, -0.9,
		 0.1,  0.1, -0.9,
		 0.1,  0.1, -0.9,
		-0.1,  0.1, -0.9,
		-0.1,  0.1, -0.9,
		-0.1, -0.1, -0.9,
	]
	let light_buffer = CreateArrayBuffer( lightVerts );
	num_light_verts = lightVerts.length / 3;

	const quadRes = 32;
	let quadVerts = new Array( (quadRes+1)*(quadRes+1)*2 );
	for ( let y=0, i=0; y<=quadRes; ++y ) {
		for ( let x=0; x<=quadRes; ++x ) {
			quadVerts[i++] = x / quadRes;
			quadVerts[i++] = y / quadRes;
		}
	}
	let quadIndices = new Array( (quadRes+1)*quadRes*2 + quadRes );
	for ( let y=0, i=0, even=true; y<quadRes; ++y, even=!even ) {
		if ( even ) {
			let j = y*(quadRes+1);
			for ( let x=0; x<=quadRes; ++x, ++j ) {
				quadIndices[i++] = j;
				quadIndices[i++] = j + quadRes + 1;
			}
		} else {
			let j = (y+1)*(quadRes+1) - 1;
			for ( let x=0; x<=quadRes; ++x, --j ) {
				quadIndices[i++] = j;
				quadIndices[i++] = j + quadRes + 1;
			}
		}
		quadIndices[i] = quadIndices[i-1];
		i++;
	}
	let quad_buffer = CreateArrayBuffer( quadVerts );
	let quad_buffer_ix = gl.createBuffer();
	gl.bindBuffer( gl.ELEMENT_ARRAY_BUFFER, quad_buffer_ix );
	gl.bufferData( gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(quadIndices), gl.STATIC_DRAW );
	num_quad_indices = quadIndices.length;

	let texquad_buffer = CreateArrayBuffer( [ 0,0, 1,0, 0,1, 1,1 ] );

	hair_buffer_pos  = gl.createBuffer();
	hair_buffer_pos2 = gl.createBuffer();
	hair_buffer_texW = gl.createBuffer();

	///////////////////////////////////////////////////////////////////////
	// Set the vertex buffers used for rendering

	vao_obj = gl.createVertexArray();
	gl.bindVertexArray(vao_obj);
	SetBuffer( prog_obj, 'pos', obj_buffer_pos, 3 );
	SetBuffer( prog_obj, 'nrm', obj_buffer_nrm, 3 );
	SetBuffer( prog_obj, 'uvs', obj_buffer_uvs, 2 );
	gl.bindBuffer( gl.ELEMENT_ARRAY_BUFFER, obj_buffer_ix );

	vao_hair = gl.createVertexArray();
	gl.bindVertexArray(vao_hair);
	//SetBuffer( prog_hair, 'pos',  hair_buffer_pos,  4 );
	//SetBuffer( prog_hair, 'pos2', hair_buffer_pos2, 4 );
	//SetBuffer( prog_hair, 'texW', hair_buffer_texW, 2 );

	vao_hairmeshC = gl.createVertexArray();
	gl.bindVertexArray(vao_hairmeshC);
	SetBuffer( prog_hairmeshC, 'pos', hmcurve_buffer_pos, 4 );

	vao_hairmeshL = gl.createVertexArray();
	gl.bindVertexArray(vao_hairmeshL);
	SetBuffer( prog_hairmeshL, 'pos', hairmeshL_buffer_pos, 2 );

	vao_light = gl.createVertexArray();
	gl.bindVertexArray(vao_light);
	SetBuffer( prog_light, 'pos', light_buffer, 3 );

	vao_clump = gl.createVertexArray();
	gl.bindVertexArray(vao_clump);
	SetBuffer( prog_clump, 'pos', quad_buffer, 2 );
	gl.bindBuffer( gl.ELEMENT_ARRAY_BUFFER, quad_buffer_ix );

	vao_tex = gl.createVertexArray();
	gl.bindVertexArray(vao_tex);
	SetBuffer( prog_tex, 'pos', texquad_buffer, 2 );

	///////////////////////////////////////////////////////////////////////
	// Create: noise texture

	createHMTex( 8, 3 );
	gl.texImage3D( gl.TEXTURE_3D, 0, gl.RGB, noise_size, noise_size, noise_size, 0, gl.RGB, gl.UNSIGNED_BYTE, new Uint8Array(noise_data) );
	gl.texParameteri( gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.REPEAT );
	gl.texParameteri( gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.REPEAT );
	gl.texParameteri( gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.REPEAT );

	///////////////////////////////////////////////////////////////////////
	// Prepare clump texture rendering

	function createClumpTexture()
	{
		let tex = gl.createTexture();
		gl.activeTexture(gl.TEXTURE9);
		gl.bindTexture  (gl.TEXTURE_2D, tex);
		gl.texImage2D   (gl.TEXTURE_2D, 0, gl.RGB8, 1, 1, 0, gl.RGB, gl.UNSIGNED_BYTE, null);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		return tex;
	}
	
	clumpTex    = createClumpTexture();
	clumpExtTex = createClumpTexture();
	clumpFramebuffer = gl.createFramebuffer();
	
	///////////////////////////////////////////////////////////////////////
	// Prepare material textures

	var mtlScript = document.createElement("script");
	mtlScript.id = "mtlScript";
	mtlScript.onload = () => {
		console.log( hairMaterial.name );
	};
	document.head.appendChild(mtlScript);
	
	LoadTextureImage("models/july.jpg", 3,0,2,[240,167,134]);
	
	///////////////////////////////////////////////////////////////////////
	// Read the initial UI settings

	InitParam();
	
	UpdateHairInstance();
	
	RenderScene();
}

//----------------------------------------------------------------------

function SimModuleReady()
{
	console.log('Initializing hair simulation module');
	simulator.sim_resize      = Module.cwrap( "sim_resize",      "",       Array(5).fill("number") );
	simulator.sim_step        = Module.cwrap( "sim_step",        "number", Array(3).fill("number") );
	simulator.sim_init        = Module.cwrap( "sim_init",        "",       ["number"] );
	simulator.sim_clear       = Module.cwrap( "sim_clear",       "",       [] );
	simulator.sim_vertPos     = Module.cwrap( "sim_vertPos",     "number", [] );
	simulator.sim_vertPrev    = Module.cwrap( "sim_vertPrev",    "number", [] );
	simulator.sim_bundles     = Module.cwrap( "sim_bundles",     "number", [] );
	simulator.sim_texHMids    = Module.cwrap( "sim_texHMids",    "number", [] );
	simulator.sim_texHMweight = Module.cwrap( "sim_texHMweight", "number", [] );
	simulator.sim_texUVids    = Module.cwrap( "sim_texUVids",    "number", [] );
	simulator.sim_tex_hm      = Module.cwrap( "sim_tex_hm",      "number", [] );
	simulator.sim_tex_uw      = Module.cwrap( "sim_tex_uw",      "number", [] );
	simulator.sim_tex_v       = Module.cwrap( "sim_tex_v",       "number", [] );
	simulator.sim_colliders   = Module.cwrap( "sim_colliders",   "number", [] );

	simulator.init = function()
	{
		return;
		console.log('Initializing hair simulation');
	
		let tex_hm_size = tex_hairmesh_ids.length / 3;

		this.sim_resize( hairmesh_verts.length, hairmesh_bundles.length, tex_hm_size, tex_uvw_ids.length, obj_colliders.length );

		let vertPos     = new Float32Array( wasmMemory.buffer, this.sim_vertPos    (), hairmesh_verts.length * 3 );
		let vertPrev    = new Int32Array  ( wasmMemory.buffer, this.sim_vertPrev   (), hairmesh_verts.length );
		let bundles     = new Int32Array  ( wasmMemory.buffer, this.sim_bundles    (), hairmesh_bundles.length * 4 );
		let texHMids    = new Int32Array  ( wasmMemory.buffer, this.sim_texHMids   (), tex_hairmesh_ids.length );
		let texHMweight = new Float32Array( wasmMemory.buffer, this.sim_texHMweight(), tex_hairmesh_ids.length );
		let texUVids    = new Int32Array  ( wasmMemory.buffer, this.sim_texUVids   (), tex_uvw_ids.length );
		let colliders   = new Float32Array( wasmMemory.buffer, this.sim_colliders  (), obj_colliders.length * 8 );
		
		for ( let i=0, j=0; i<hairmesh_verts.length; ++i ) {
			vertPos[j++] = hairmesh_verts[i].p[0];
			vertPos[j++] = hairmesh_verts[i].p[1];
			vertPos[j++] = hairmesh_verts[i].p[2];
			vertPrev[i]  = hairmesh_verts[i].prev;
		}
		
		for ( let i=0, j=0; i<hairmesh_bundles.length; ++i ) {
			let n = hairmesh_bundles[i].length;
			if ( n > 4 ) n = 4;
			for ( let k=0; k<n; ++k ) bundles[j++] = hairmesh_bundles[i][k];
			for ( let k=n; k<4; ++k ) bundles[j++] = -1;
		}

		for ( let i=0; i<tex_hairmesh_weights.length; ++i ) {
			texHMids   [i] = tex_hairmesh_ids    [i];
			texHMweight[i] = tex_hairmesh_weights[i];
		}

		for ( let i=0; i<tex_uvw_ids.length; ++i ) {
			texUVids[i] = tex_uvw_ids[i];
		}
		
		for ( let i=0, j=0; i<obj_colliders.length; ++i ) {
			for ( let k=0; k<8; ++k, ++j ) {
				colliders[j] = obj_colliders[i][k];
			}
		}
		
		this.sim_init( objRot );
	
		this.tex_array_hm = new Float32Array( wasmMemory.buffer, this.sim_tex_hm(), tex_hm_size * 3 );
		this.tex_array_uw = new Float32Array( wasmMemory.buffer, this.sim_tex_uw(), tex_uvw_ids.length * 4 );
		this.tex_array_v  = new Float32Array( wasmMemory.buffer, this.sim_tex_v(),  tex_uvw_ids.length * 3 );
		
		this.run();
	};
	
	simulator.timer = false;
	
	simulator.run = function()
	{
		//console.log('simulation.run');
		if ( this.timer === false ) {
			this.stepCounter = 0;
			this.timer = window.setInterval( function() { simulator.step(); }, this.stepSize );
		}
	};

	simulator.isRunning = function() { return ( this.timer !== false ); };
	
	simulator.step = function()
	{
		//console.log('simulation.step');
		let success = this.sim_step( objRot, this.stepSize/1000, this.numIter );
		//this.stepCounter++;
		//if ( this.stepCounter > 1000 ) success = false;
		if ( success ) {
			gl.activeTexture( gl.TEXTURE4 );
			gl.texSubImage3D( gl.TEXTURE_3D, 0,0,0,0, tex_hairmesh_width, tex_hairmesh_height, tex_hairmesh_depth, gl.RGB, gl.FLOAT, this.tex_array_hm );
			//gl.activeTexture( gl.TEXTURE6 );
			//gl.texSubImage3D( gl.TEXTURE_3D, 0,0,0,0, tex_hairmesh_width, tex_hairmesh_height, maxLevel+1, gl.RGBA, gl.FLOAT, this.tex_array_uw );
			//gl.activeTexture( gl.TEXTURE7 );
			//gl.texSubImage3D( gl.TEXTURE_3D, 0,0,0,0, tex_hairmesh_width, tex_hairmesh_height, maxLevel+1, gl.RGB, gl.FLOAT, this.tex_array_v );
			//Refresh();
			window.requestAnimationFrame(RenderScene);
		} else {
			window.clearInterval( this.timer );
			this.timer = false;
		}
	};

	simulator.stepSize = 16;	// milliseconds
	simulator.numIter  = 8;
	simulator.moduleReady = true;
	
	if ( canvas !== false ) simulator.init();
}

//----------------------------------------------------------------------

window.onload = function()
{
	console.log('Initializing hair rendering');
	InitRendering();
	simulator.init();
}