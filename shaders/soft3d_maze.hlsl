// Soft3D Maze HLSL
// fxc: shaders\compile_shaders.cmd → res\cso\
// 実行時は埋め込み CSO を読む（無ければこのファイルから D3DCompile）

cbuffer F:register(b0){row_major float4x4 VP;row_major float4x4 LightVP;row_major float4x4 ReflectVP;row_major float4x4 ReflectFloorVP;float4 Eye;float4 Fog;float4 Dof;float4 Screen;float4 Misc;float4 LightDir;float4 Wind;}
cbuffer Skin:register(b1){row_major float4x4 Bones[16];}
Texture2D T0:register(t0);Texture2D T1:register(t1);Texture2D Depth:register(t2);
TextureCube Env:register(t3);Texture2D ShadowMap:register(t4);Texture2D MirrorMap:register(t5);Texture2D MirrorFloor:register(t6);Texture2D FxMap:register(t7);
SamplerState SL:register(s0);SamplerState SP:register(s1);SamplerComparisonState SCmp:register(s2);
struct V{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};
struct P{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};
struct D{float4 p:SV_POSITION;float3 w:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float4 c:TEXCOORD3;};
P VST(V x){P o;o.p=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;return o;}
struct HC{float e[4]:SV_TessFactor;float i[2]:SV_InsideTessFactor;};
HC HPC(InputPatch<P,4> p,uint id:SV_PrimitiveID){HC o;float3 c=(p[0].p+p[1].p+p[2].p+p[3].p)*.25;float d=distance(c,Eye.xyz);
float tf=(LightDir.w<.5)?5.:lerp(12.,3.4,saturate((d-1.2)/14.));tf=clamp(tf,3.5,14.);
o.e[0]=o.e[1]=o.e[2]=o.e[3]=tf;o.i[0]=o.i[1]=tf;return o;}
[domain("quad")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(4)][patchconstantfunc("HPC")]
P HST(InputPatch<P,4> p,uint i:SV_OutputControlPointID,uint id:SV_PrimitiveID){return p[i];}
float hash(float2 p){return frac(sin(dot(p,float2(12.9898,78.233)))*43758.5453);}
float noise(float2 p){float2 i=floor(p),f=frac(p);float a=hash(i),b=hash(i+float2(1,0)),c=hash(i+float2(0,1)),d=hash(i+float2(1,1));
float2 u=f*f*(3.-2.*f);return lerp(a,b,u.x)+(c-a)*u.y*(1.-u.x)+(d-b)*u.x*u.y;}
float fbm(float2 p){float f=0.,a=0.5;for(int i=0;i<4;i++){f+=a*noise(p);p*=2.;a*=.5;}return f;}
float Dggx(float nh,float a){float a2=max(a*a,.002);float d=nh*nh*(a2-1.)+1.;return a2/(3.14159265*d*d);}
float3 Fres(float3 F0,float vh){return F0+(1.-F0)*pow(1.-saturate(vh),5.);}
float Gsch(float nv,float nl,float a){float k=pow(a+1.,2.)/8.;return (nv/(nv*(1.-k)+k))*(nl/(nl*(1.-k)+k));}
float3 SpecB(float3 n,float3 l,float3 v,float rough,float3 F0){float3 h=normalize(l+v);float nh=saturate(dot(n,h)),nv=saturate(dot(n,v)),nl=saturate(dot(n,l)),vh=saturate(dot(v,h));float a=max(rough*rough,.002);return Dggx(nh,a)*Gsch(nv,nl,a)*Fres(F0,vh)/max(4.*nv*nl,1e-4);}
float3 EnvR(float3 n,float3 v,float rough){return Env.SampleLevel(SL,reflect(-v,n),clamp(rough*5.5,0.,7.)).rgb;}
float2 POM(float2 uv,float3 vw,float3 n,float s){float3 t=normalize(cross(n,abs(n.y)>.9?float3(1,0,0):float3(0,1,0)));float2 du=float2(dot(vw,t),-vw.y)*s;[unroll]for(int i=0;i<8;i++)uv-=du*(lerp(T0.Sample(SL,uv).a,T1.Sample(SL,uv).r,.55)-.44);return uv;}
float ContAO(float3 n){float g=length(float2(ddx(n.y),ddy(n.y)));return saturate(1.-g*4.0);}
[domain("quad")]D DST(HC h,float2 q:SV_DomainLocation,const OutputPatch<P,4> p){
P a,b,o;a.p=lerp(p[0].p,p[1].p,q.x);b.p=lerp(p[3].p,p[2].p,q.x);o.p=lerp(a.p,b.p,q.y);
a.n=lerp(p[0].n,p[1].n,q.x);b.n=lerp(p[3].n,p[2].n,q.x);o.n=normalize(lerp(a.n,b.n,q.y));
a.uv=lerp(p[0].uv,p[1].uv,q.x);b.uv=lerp(p[3].uv,p[2].uv,q.x);o.uv=lerp(a.uv,b.uv,q.y);o.c=p[0].c;
float nrm=fbm(o.p.xz*3.2+o.p.y*2.4)-.5;
float pulse=sin(Misc.w*1.55+o.p.x*1.7+o.p.z*1.4)*0.016*(0.55+0.45*Wind.w);
if(LightDir.w>=0.5){o.p+=o.n*(nrm*0.038+pulse*saturate(1.05-abs(o.n.y)));}
D z;z.w=o.p;z.n=o.n;z.uv=o.uv;z.c=o.c;z.p=mul(float4(o.p,1),VP);return z;}
[maxvertexcount(3)]void GSW(triangle D i[3],inout TriangleStream<D> s){
float y0=min(i[0].w.y,min(i[1].w.y,i[2].w.y));
[unroll]for(int k=0;k<3;k++){D o=i[k];float h=saturate((o.w.y-y0)*2.2);
float sway=sin(Misc.w*1.4+o.w.x*1.8+o.w.z*1.5)*Wind.w*h;o.w.xz+=Wind.xz*sway*0.035;o.p=mul(float4(o.w,1),VP);s.Append(o);}s.RestartStrip();}
float ShadowAt(float3 w,float3 n){float3 nn=normalize(n);float3 l=normalize(LightDir.xyz);float ndl=saturate(dot(nn,l));
w+=nn*(0.018+(1.-ndl)*0.028);float4 sp=mul(float4(w,1),LightVP);float iw=1.0/max(sp.w,1e-5);
float2 uv=sp.xy*iw*float2(.5,-.5)+.5;
float z=sp.z*iw-0.0024;
if(any(uv<0)||any(uv>1)||z<=0||z>=1)return 1;
const float2 o[12]={float2(-0.326,-0.406),float2(-0.840,-0.074),float2(-0.696,0.457),float2(-0.203,0.621),
float2(0.962,-0.195),float2(0.473,-0.480),float2(0.519,0.767),float2(0.185,-0.893),
float2(0.507,0.064),float2(0.896,0.412),float2(-0.458,-0.882),float2(-0.054,0.937)};
float s=0,pen=lerp(1.15,2.8,1.-ndl);const float t=pen/1024.0;
[unroll]for(int k=0;k<12;k++)s+=ShadowMap.SampleCmpLevelZero(SCmp,uv+o[k]*t,z);
s*=0.08333;return pow(saturate(s),1.18);}
float ShadeLit(float ndl,float sh){float d=saturate(ndl);float wrap=saturate(ndl*.48+.52);float amb=.26+.10*saturate(Eye.w*.5);
float lit=lerp(amb,max(d,amb*.48),sh);return saturate(lit*.72+wrap*wrap*.36);}
float4 PlanarMir(float3 w,row_major float4x4 RVP,Texture2D M){float4 rp=mul(float4(w,1),RVP);float iw=max(rp.w,1e-5);float2 muv=rp.xy/iw*float2(.5,-.5)+.5;
float mb=(rp.w>0)*(muv.x>=0)*(muv.x<=1)*(muv.y>=0)*(muv.y<=1)*saturate(min(min(muv.x,1-muv.x),min(muv.y,1-muv.y))*12);return float4(M.Sample(SL,saturate(muv)).rgb,mb);}
float4 PSW(D i):SV_Target{float3 v=normalize(Eye.xyz-i.w);float3 n0=normalize(i.n);
float2 uv=POM(i.uv*2.35,v,n0,.030);float4 a=T0.Sample(SL,uv)*i.c;float h=a.a;
float3 det=T1.Sample(SL,uv*3.6).rgb;a.rgb=lerp(a.rgb,saturate(a.rgb*det*1.34),0.55);
float hx=T0.Sample(SL,uv+float2(.0035,0)).a-h;float hy=T0.Sample(SL,uv+float2(0,.0035)).a-h;
float nz=fbm(uv*210.+Misc.w*.02)*0.09;
float3 n=normalize(n0+float3(hx+nz,hy+nz,0)*5.4);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);
float nd=ShadeLit(dot(n,l),sh);
float3 env=EnvR(n,v,lerp(.46,.18,saturate(1.-abs(n.y))));
float metal=saturate((i.c.a-1.01)*8);float doorM=saturate(1-abs(i.c.a-1.05)*50);float keyM=saturate(1-abs(i.c.a-1.12)*40);
float useMir=LightDir.w;
float4 mir=PlanarMir(i.w,ReflectVP,MirrorMap);float4 mir2=PlanarMir(i.w+reflect(-v,n)*1.2,ReflectVP,MirrorMap);if(mir2.a>mir.a)mir=mir2;
float mw=metal*useMir*max(mir.a,.35);env=lerp(env,lerp(env,mir.rgb,saturate(mir.a)),mw);
float pulse=.5+.5*sin(Misc.w*1.65+i.w.x*.4+i.w.z*.3);
float occ=lerp(0.78,1.12,a.a)*ContAO(n);if(Eye.w>0.5)occ=lerp(0.88,1.18,a.a);
float3 F0c=lerp(float3(.045,.045,.05),float3(.74,.76,.80),metal);
float3 hemi=lerp(float3(.36,.32,.30),float3(.64,.76,.94),saturate(n.y*.5+.5));
float3 col=lerp(a.rgb*nd*occ*lerp(float3(1,1,1),hemi,.24),mir.rgb*(.20+.80*a.rgb),mw*.92);
col+=SpecB(n,l,v,lerp(.50,.14,metal),F0c)*sh*2.55+env*(.13+.30*metal)*lerp(.48,1,sh);
col+=float3(1,.94,.78)*doorM*(.42+.58*pulse)*sh*.62;col+=float3(1,.92,.32)*keyM*(.52+.48*pulse)*sh;
col=lerp(col,env*(.38+.62*a.rgb)+mir.rgb*.52,keyM*.38*useMir);
float corner=saturate(1.-abs(n.y)*1.15)*.18;col*=1.-corner;
float str=0;if(abs(n.y)<.50&&Eye.w>=.5)str=smoothstep(.55,.92,fbm(float2(i.w.x+i.w.z,i.w.y*2.3-Misc.w*.05)*3.8));
col=lerp(col,col*float3(.82,.90,.96)+float3(1,.94,.80)*str*.22,str*.30);
float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=saturate(fg+max(0,Fog.w-i.w.y)*Fog.z);fg=fg*fg*(3-2*fg);
return float4(lerp(col,float3(.50,.64,.82),fg*.58),1);}
D VSS(V x){D o;o.w=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;o.p=mul(float4(x.p,1),VP);return o;}
struct VK{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;float4 sk:TEXCOORD2;};
D VSSKIN(VK x){uint bi=min((uint)x.sk.w,15);float bw=saturate(frac(x.sk.w));float3 root=x.sk.xyz;float3 lp=x.p-root;
float3 sp=lerp(lp,mul(float4(lp,1),Bones[bi]).xyz,bw);float3 sn=lerp(x.n,mul(x.n,(float3x3)Bones[bi]),bw);
float dist=length(lp);float tip=bw*bw;sp.y-=tip*dist*0.16;
float fl=sin(Misc.w*2.28+root.x*5.05+root.z*4.22+dist*8.4)*Wind.w;
sp.xz+=Wind.xz*fl*tip*0.058;sp.y+=fl*tip*0.012;
float leaf=saturate(1.-abs(x.c.a-0.97)*70.);float lf=sin(Misc.w*3.15+root.x*7.2+x.uv.x*9.+dist*11.)*Wind.w;
sp.xz+=Wind.xz*lf*leaf*0.045;sp.y+=lf*leaf*0.018;
D o;o.w=root+sp;o.n=normalize(sn);o.uv=x.uv;o.c=x.c;o.p=mul(float4(o.w,1),VP);return o;}
float4 PSS(D i):SV_Target{float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);
float nz=fbm(i.w.xz*18.+i.w.y*14.+Misc.w*.02)*0.1;
n=normalize(n+float3(nz,nz*0.5,nz)*0.8);
float3 v=normalize(Eye.xyz-i.w);float nd=ShadeLit(dot(n,l),sh);float gro=Eye.w<.5?1.:0.;
float mirror=saturate((i.c.a-1.01)*8);float glass=saturate((i.c.a-1.18)*10);float useMir=LightDir.w;
float doorM=saturate(1-abs(i.c.a-1.05)*50);float keyM=saturate(1-abs(i.c.a-1.12)*40);
float4 mir=PlanarMir(i.w,ReflectFloorVP,MirrorFloor);float4 mir2=PlanarMir(i.w+n*.55,ReflectFloorVP,MirrorFloor);if(mir2.a>mir.a)mir=mir2;
float floorM=saturate((i.c.a-1.13)*28);
float trapK=saturate(1.-abs(i.c.a-.44)*16.);float itemK=saturate(1.-abs(i.c.a-1.15)*18.);
float glassK=saturate((i.c.a-1.18)*8.);float woodK=saturate(1.-abs(i.c.a-1.05)*40.);
float prop=saturate(trapK+itemK+glassK+woodK);
float flrK=saturate(n.y)*saturate(1.-prop-mirror);
float2 suv=lerp(i.uv*4.2,i.w.xz*0.52+i.w.y*0.20,flrK);suv=lerp(suv,i.uv,prop);
suv=lerp(suv,POM(suv,v,n,lerp(.022,.007,gro)),flrK);
float4 tex4=T0.Sample(SL,suv);if(itemK>0.4&&tex4.a<0.10)discard;float3 tex=tex4.rgb;
float3 det=T1.Sample(SL,lerp(suv*2.6,i.uv,prop)).rgb;float tm=saturate(dot(tex,tex)*3.);float dm=saturate(dot(det,det)*3.);
float3 albedo=lerp(i.c.rgb,saturate(tex*lerp(float3(1,1,1),det*1.36,0.48*dm)),lerp(0.70*tm,0.96,itemK));
albedo=lerp(albedo,saturate(lerp(tex,det,trapK)*i.c.rgb*1.12),prop*(1.-itemK));
albedo=lerp(albedo,saturate(tex*i.c.rgb*1.08),itemK);
if(gro>0&&flrK>.35){float2 tp=abs(frac(i.w.xz*2.15)-.5);float grout=smoothstep(.38,.47,max(tp.x,tp.y));
float2 cid=floor(i.w.xz*7.3);float fl=hash(cid);float flw=smoothstep(.82,1.,fl)*smoothstep(.12,0.,length(frac(i.w.xz*7.3)-.5));
albedo=saturate(albedo*float3(1.16,1.14,1.02)+float3(.10,.12,.04)*flrK);
albedo=lerp(albedo,float3(.52,.78,.38),grout*.42*flrK);
albedo=lerp(albedo,lerp(float3(.98,.45,.62),float3(.98,.82,.28),step(.5,hash(cid+.17))),flw*.72*flrK);}
float3 hemi=lerp(gro>0?float3(.55,.52,.40):float3(.36,.33,.31),float3(.72,.82,.95),saturate(n.y*.5+.5));
float3 lit=albedo*nd*lerp(float3(1,1,1),hemi,.22)*ContAO(n);
float pud=0;if(n.y>.55&&gro<1)pud=smoothstep(.50,.86,fbm(i.w.xz*3.1))*smoothstep(.22,.02,i.w.y);
float str=0;if(abs(n.y)<.45&&gro<1)str=smoothstep(.62,.92,fbm(float2(i.w.x+i.w.z,i.w.y*2.1-Misc.w*.07)*4.1));
float wet=saturate(pud*.9+str*.5)*(1-mirror)*(1-floorM);
float3 F0c=lerp(float3(.04,.04,.04),float3(.78,.80,.84),saturate(mirror+keyM+floorM));
float3 chrome=lerp(EnvR(n,v,lerp(.42,.12,saturate(mirror+floorM)))*1.08,mir.rgb*1.22,saturate(mir.a));
float mw=max(mirror*useMir*saturate(mir.a),floorM*useMir);
mw=max(mw,keyM*useMir*saturate(mir.a));
float glowP=.55+.45*sin(Misc.w*2.1+i.w.y*3);
float3 c=lerp(lit,chrome*(.06+.94*saturate(i.c.rgb+.32)),max(mw,floorM*useMir)*(.99-.12*glass));
c+=EnvR(n,v,lerp(.55,.16,saturate(mirror+itemK)))*((.12+.42*(mirror+floorM))*(1-mw*.85)+mirror*.20)*lerp(.48,1,sh);
c+=SpecB(n,l,v,lerp(.58,.14,saturate(mirror+itemK+wet)),F0c)*sh*lerp(1.7,2.6,itemK);
c+=float3(.7,.9,1)*pow(saturate(1.-dot(n,v)),2.2)*mirror*.45;
c=lerp(c,EnvR(n,v,.12)*(.45+.55*saturate(i.c.rgb+.2))+mir.rgb*.55,keyM*(.40+.28*mirror)*useMir);
c+=float3(.85,.95,1)*SpecB(n,l,v,.18,float3(.03,.04,.05))*wet*sh;
c+=float3(1,.9,.32)*keyM*(.48+.52*glowP)*.85;c+=float3(.75,.7,.65)*doorM*sh*.35;
float leaf=saturate(1.-abs(i.c.a-0.97)*70.);c=lerp(c,c*float3(.82,1.08,.7)+EnvR(n,v,.55)*.10,leaf*.42);
c+=i.c.rgb*(.04+.05*glowP)*saturate(i.c.a-.35)*(1-mirror)*.28;
float al=mirror>0?lerp(lerp(.96,.90,mw),lerp(.84,.74,mw),glass):saturate(i.c.a);
al=lerp(al,saturate(max(tex4.a,0.88)),itemK);al=max(al,saturate(0.94*flrK));
float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));
fg=saturate(fg+max(0,Fog.w-i.w.y)*Fog.z);fg=fg*fg*(3-2*fg);
return float4(lerp(c,float3(.50,.64,.82),fg*.48*(1-mw*.7)),al);}
float4 PSMIRF(D i):SV_Target{
float4 rp=mul(float4(i.w,1),ReflectFloorVP);float iw=max(rp.w,1e-5);
float2 uv=rp.xy/iw*float2(.5,-.5)+.5;
float vis=(rp.w>0)*(uv.x>0)*(uv.x<1)*(uv.y>0)*(uv.y<1);
float3 img=T0.Sample(SL,saturate(uv)).rgb;
float3 flr=T1.Sample(SL,i.uv*4.2).rgb*saturate(i.c.rgb+.12);
float3 env=Env.Sample(SL,reflect(-normalize(Eye.xyz-i.w),normalize(i.n))).rgb;
float3 mir=lerp(env*.42,img,vis);
float fr=pow(1.-saturate(dot(normalize(i.n),normalize(Eye.xyz-i.w))),2.4);
float k=0.70+0.12*fr;
float3 c=lerp(flr,mir,k);
return float4(c,1);}
float4 PSCLoud(D i):SV_Target{
float4 t0=T0.Sample(SL,i.uv);float2 j=float2(.02,-.015);
float4 t1=T0.Sample(SL,saturate(i.uv+j));float dens=saturate(max(t0.a,t1.a*.85)*i.c.a);if(dens<0.07)discard;
float3 albedo=saturate((t0.rgb*.7+t1.rgb*.3)*i.c.rgb);
float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float3 v=normalize(Eye.xyz-i.w);
float nl=saturate(dot(n,l)*.5+.5);float thin=dens*dens*(1.2-dens);
float silver=pow(saturate(1.-saturate(dot(n,v))),2.2)*pow(saturate(dot(v,l)*.5+.5),3.)*dens;
float3 sunC=float3(1.02,.94,.82);float3 skyC=float3(.5,.64,.9);
float3 c=albedo*lerp(skyC,sunC,nl*.5+.3)*(.6+.5*nl)+sunC*pow(thin,1.3)*1.35+sunC*silver;
float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);
return float4(lerp(c,float3(.52,.66,.84),fg*.4),saturate(dens*0.90));}
struct HV{float2 p:POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};struct HO{float4 p:SV_POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};
HO VSH(HV x){HO o;o.p=float4(x.p,0,1);o.uv=x.uv;o.c=x.c;return o;}
float4 PSH(HO i):SV_Target{if(i.uv.x<-0.5)return i.c;float4 t=T0.Sample(SL,i.uv);return float4(t.rgb*i.c.rgb,t.a*i.c.a);}
float4 PSLINE(HO i):SV_Target{float t=saturate(1.-abs(i.uv.y-.5)*2.4);float cap=saturate(min(i.uv.x,1.-i.uv.x)*10.);return float4(i.c.rgb,i.c.a*t*cap);}
struct Q{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};Q VSQ(uint id:SV_VertexID){Q o;float2 p=float2((id==2)?3:-1,(id==1)?3:-1);o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,(1-p.y)*.5);return o;}
float4 SSR(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);float z=Depth.Sample(SP,i.uv).r;
float4 fx=FxMap.Sample(SL,i.uv+float2(frac(Misc.w*.06),0)+Wind.xz*.004);float th=Eye.w;
float2 oc=i.uv+float2(.0018,-.0024);float cs=saturate((Depth.Sample(SP,oc).r-z)*88.);c.rgb*=lerp(.58,1.,1.-cs*.58);
float ao=1;const float2 aoO[6]={float2(.005,.002),float2(-.004,.0035),float2(.003,-.005),float2(-.005,-.002),float2(.006,0),float2(0,.006)};
[unroll]for(int a=0;a<6;a++){float zd=Depth.Sample(SP,saturate(i.uv+aoO[a])).r;ao-=saturate((z-zd)*28.)*0.08;}c.rgb*=lerp(.68,1.,saturate(ao));
float2 rp=i.uv;float2 rd=float2((i.uv.x-.5)*.036,.016);float3 rc=c.rgb;float rk=0;
[unroll]for(int s=0;s<16;s++){rp+=rd;if(any(rp<0)||any(rp>1))break;float dz=Depth.Sample(SP,rp).r;if(dz<z-0.0016){rc=T0.Sample(SL,rp).rgb;rk=exp(-s*.11);break;}}
c.rgb=lerp(c.rgb,rc,rk*0.38*smoothstep(.08,.78,z));
float2 sun=th<.5?float2(.58,.11):float2(.5,.2);float2 dir=sun-i.uv;float rays=0;float2 p=i.uv;
[unroll]for(int k=0;k<16;k++){p+=dir*.018;if(any(p<0)||any(p>1))break;rays+=saturate(.13-Depth.Sample(SP,p).r)*exp(-k*.12);}
c.rgb+=(th<.5?float3(1,.93,.7):float3(.5,.72,1))*rays*(th<.5?.3:.18);
if(th<.5){float rain=fx.g*smoothstep(.18,.95,1.-z)*smoothstep(.78,.12,i.uv.y);c.rgb=lerp(c.rgb,float3(.7,.82,.96),rain*.12);
c.rgb+=float3(.82,.9,1)*fx.g*fx.b*.1;float2 st=i.uv+float2(.0015,-frac(Misc.w*1.55+i.uv.x*18.)*.04)+Wind.xz*.002;
c.rgb+=FxMap.Sample(SL,st).g*float3(.68,.8,.95)*smoothstep(.25,1.,1.-z)*smoothstep(.7,.1,i.uv.y)*.08;}
else{float bead=pow(saturate(fx.r*fx.b),4.)*smoothstep(.15,.85,1.-z);c.rgb+=float3(.65,.88,.95)*bead*.05;}
float3 bl=T0.Sample(SL,i.uv+Screen.zw*6.).rgb+T0.Sample(SL,i.uv-Screen.zw*6.).rgb;
float lum=dot(c.rgb,float3(.3,.5,.2));c.rgb=lerp(c.rgb,max(c.rgb,bl*.42),saturate(lum-.55)*.2);
return float4(c.rgb,1);}
float4 DOFP(Q i):SV_Target{float zd=Depth.Sample(SP,i.uv).r;const float zn=.05,zf=80.;
float eyeZ=zn*zf/max(1e-4,zf-zd*(zf-zn));
float coc=saturate((eyeZ-Dof.x)/max(.05,Dof.y));coc=coc*coc*(3.-2.*coc);
float b=coc*Dof.z;if(b<0.35)return T0.Sample(SL,i.uv);float2 px=float2(b,b)*Screen.zw;
float4 c=T0.Sample(SL,i.uv)*.28;
c+=(T0.Sample(SL,i.uv+float2(px.x,0))+T0.Sample(SL,i.uv-float2(px.x,0))+T0.Sample(SL,i.uv+float2(0,px.y))+T0.Sample(SL,i.uv-float2(0,px.y)))*.13;
c+=(T0.Sample(SL,i.uv+px)+T0.Sample(SL,i.uv-px)+T0.Sample(SL,i.uv+float2(px.x,-px.y))+T0.Sample(SL,i.uv+float2(-px.x,px.y)))*.05;
return c;}
float4 FIN(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);if(Misc.z>8.f){c.a*=saturate(Misc.y);return c;}float v=saturate(1-dot((i.uv-.5)*1.12,(i.uv-.5)*1.12));c.rgb*=lerp(.86,1.12,v);
float th=Eye.w;float3 tone=float3(1.08,1.03,.97);if(th>2.5)tone=float3(1.16,1.05,.96);else if(th>1.5)tone=float3(1.12,1.06,1.00);else if(th>.5)tone=float3(1.10,1.12,1.16);
c.rgb=saturate(c.rgb*tone);float3 x=max(c.rgb,0);c.rgb=saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));
c.rgb=lerp(c.rgb,c.rgb*c.rgb*(3.-2.*c.rgb),0.08);
float lum=dot(c.rgb,float3(.299,.587,.114));c.rgb+=c.rgb*saturate(lum-.82)*.10;
float cas=saturate(Dof.w);if(cas>0.01){float2 px=Screen.zw*1.25f;float3 soft=T0.Sample(SL,i.uv+float2(px.x,0)).rgb+T0.Sample(SL,i.uv-float2(px.x,0)).rgb+T0.Sample(SL,i.uv+float2(0,px.y)).rgb+T0.Sample(SL,i.uv-float2(0,px.y)).rgb;soft*=.25;c.rgb=saturate(c.rgb+(c.rgb-soft)*cas);}
float2 ca=Screen.zw*(0.7+1.4*length(i.uv-.5));c.r=lerp(c.r,T0.Sample(SL,saturate(i.uv+ca)).r,.08);c.b=lerp(c.b,T0.Sample(SL,saturate(i.uv-ca)).b,.08);
float gr=frac(sin(dot(i.uv,float2(12.9898,78.233))+Misc.w*0.7)*43758.5453);c.rgb+=(gr-.5)*0.012;
float4 fx=FxMap.Sample(SL,i.uv);if(th<.5)c.rgb+=float3(.12,.2,.38)*fx.a*saturate(1.05-i.uv.y)*0.10;
return c;}
RWTexture2D<float4> FxOut:register(u0);
[numthreads(8,8,1)]void CSFx(uint3 id:SV_DispatchThreadID){
uint2 p=id.xy;if(p.x>=256||p.y>=256)return;float2 uv=(float2(p)+.5)/256.;float t=Misc.w;
float n=fbm(uv*7.+t*.12);float n2=fbm(uv*19.-t*.38);
float col=smoothstep(.62,1.,frac(uv.x*26.+n*1.4));
float drip=col*smoothstep(.12,.95,frac(uv.y*8.-t*1.38+n2));
float rain=smoothstep(.55,1.,frac(uv.x*42.+n*2.1))*smoothstep(.1,.92,frac(uv.y*16.-t*2.05+n2));
float spark=pow(saturate(n*n2),3.);float sky=saturate(.12+n*.55+(1.-uv.y)*.42);
FxOut[p]=float4(drip,rain,spark,sky);}
