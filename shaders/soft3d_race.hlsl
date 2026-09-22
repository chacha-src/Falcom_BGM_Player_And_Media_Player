// Soft3D Race HLSL
// fxc: shaders\compile_shaders.cmd → res\cso\
// 実行時は埋め込み CSO を読む（無ければこのファイルから D3DCompile）

cbuffer F:register(b0){row_major float4x4 VP;row_major float4x4 LightVP;float4 Eye;float4 Fog;float4 Dof;float4 Screen;float4 Misc;float4 LightDir;float4 Peel;row_major float4x4 ReflectVP;float4 Wind;}
cbuffer Skin:register(b1){row_major float4x4 Bones[16];}
Texture2D T0:register(t0);Texture2D T1:register(t1);Texture2D Depth:register(t2);TextureCube Env:register(t3);Texture2D ShadowMap:register(t4);Texture2D NoiseMap:register(t5);Texture2D ReflectMap:register(t6);
SamplerState SL:register(s0);SamplerState SP:register(s1);SamplerComparisonState SCmp:register(s2);
struct V{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};
struct P{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};
struct D{float4 p:SV_POSITION;float3 w:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float4 c:TEXCOORD3;};
P VST(V x){P o;o.p=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;return o;}
struct HC{float e[4]:SV_TessFactor;float i[2]:SV_InsideTessFactor;};
HC HPC(InputPatch<P,4> p,uint id:SV_PrimitiveID){HC o;float3 c=(p[0].p+p[1].p+p[2].p+p[3].p)*.25;
float d=distance(c,Eye.xyz);float tf=(LightDir.w<.5)?3.:lerp(11.,2.2,saturate((d-10.)/85.));tf=clamp(tf,2.,12.);
o.e[0]=o.e[1]=o.e[2]=o.e[3]=tf;o.i[0]=o.i[1]=tf;return o;}
[domain("quad")][partitioning("fractional_even")][outputtopology("triangle_cw")][outputcontrolpoints(4)][patchconstantfunc("HPC")]
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
float2 POM(float2 uv,float3 vw,float3 n,float s){float3 t=normalize(cross(n,abs(n.y)>.9?float3(1,0,0):float3(0,1,0)));float2 du=float2(dot(vw,t),-vw.y)*s;[unroll]for(int i=0;i<6;i++)uv-=du*(T1.Sample(SL,uv).r-.48);return uv;}
float ContAO(float3 n){float g=length(float2(ddx(n.y),ddy(n.y)));return saturate(1.-g*3.2);}
float PeelAmt(float3 w){float rad=abs(Peel.w);if(rad<.05)return 0;float3 e=Eye.xyz;float3 pl=Peel.xyz-e;float lp=length(pl);if(lp<1.1)return 0;
float3 dir=pl/lp;float3 tw=w-e;float along=dot(tw,dir);float dl=length(tw-dir*along);
if(along<0.65||along>lp-0.85||dl>rad)return 0;float ka=saturate((along-0.65)/1.8);float kb=saturate((lp-0.85-along)/2.8);float kr=saturate(1.-dl/rad);return ka*kb*kr*kr;}
[domain("quad")]D DST(HC h,float2 q:SV_DomainLocation,const OutputPatch<P,4> p){
P a,b,o;a.p=lerp(p[0].p,p[1].p,q.x);b.p=lerp(p[3].p,p[2].p,q.x);o.p=lerp(a.p,b.p,q.y);
a.n=lerp(p[0].n,p[1].n,q.x);b.n=lerp(p[3].n,p[2].n,q.x);o.n=normalize(lerp(a.n,b.n,q.y));
a.uv=lerp(p[0].uv,p[1].uv,q.x);b.uv=lerp(p[3].uv,p[2].uv,q.x);o.uv=lerp(a.uv,b.uv,q.y);o.c=p[0].c;
float disp=(fbm(o.p.xz*0.05+Misc.w*0.018)-.48)*0.62+(fbm(o.p.xz*0.18+o.p.y*0.04)-.5)*0.16;
float wgv=sin(Misc.w*1.12+o.p.x*0.035+o.p.z*0.028)*Wind.w;
o.p+=o.n*(disp*0.48+wgv*0.10);
D z;z.w=o.p;z.n=o.n;z.uv=o.uv;z.c=o.c;z.p=mul(float4(o.p,1),VP);return z;}
[maxvertexcount(3)]void GSW(triangle D i[3],inout TriangleStream<D> s){
float y0=min(i[0].w.y,min(i[1].w.y,i[2].w.y));
[unroll]for(int k=0;k<3;k++){D o=i[k];float h=saturate((o.w.y-y0)*0.62);h=h*h;
float sway=sin(Misc.w*1.12+o.w.x*0.048+o.w.z*0.041)*Wind.w;float gust=sin(Misc.w*2.28+o.w.x*0.11)*Wind.w*0.32;
o.w.xz+=Wind.xz*(sway*0.42+gust)*h;o.w.y+=sway*h*0.06;o.p=mul(float4(o.w,1),VP);s.Append(o);}s.RestartStrip();}
float ShadowAt(float3 w,float3 n){float3 nn=normalize(n);float3 l=normalize(LightDir.xyz);float ndl=saturate(dot(nn,l));
w+=nn*(0.018+(1.-ndl)*0.04);float4 sp=mul(float4(w,1),LightVP);float iw=1.0/max(sp.w,1e-5);
float2 uv=sp.xy*iw*float2(.5,-.5)+.5;float z=sp.z*iw-0.0024;
if(any(uv<0)||any(uv>1)||z<=0||z>=1)return lerp(.55,1.,saturate(ndl));
const float2 o[12]={float2(-.326,-.406),float2(-.84,-.074),float2(-.696,.457),float2(-.203,.621),float2(.962,-.195),float2(.473,-.48),float2(.519,.767),float2(.185,-.893),float2(.507,.064),float2(.896,.412),float2(-.458,-.877),float2(.145,.294)};
float s=0;float t=lerp(1.1,2.8,1.-ndl)/1024.;[unroll]for(int k=0;k<12;k++)s+=ShadowMap.SampleCmpLevelZero(SCmp,uv+o[k]*t,z);
float sh=pow(saturate(s/12.),1.18);return lerp(.22,1.,sh);}
float4 PSB(D i):SV_Target{if(Fog.w>0.5&&i.w.y<Fog.z+0.08)discard;float4 a=T0.Sample(SL,i.uv)*i.c;float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);
float nd=lerp(.12,max(saturate(dot(n,l)),.2),sh);float3 v=normalize(Eye.xyz-i.w);float F=0.06+(1.-0.06)*pow(1.-saturate(dot(n,v)),4.2);
float3 env=Env.Sample(SL,reflect(-v,n)).rgb;float pulse=.55+.45*sin(i.uv.x*22.-Misc.w*2.4);
float3 col=a.rgb*nd+EnvR(n,v,.18)*(.08+F*.32)+SpecB(n,l,v,.22,float3(.08,.08,.08))*sh*2.2;
col*=lerp(0.62,1.0,sh)*ContAO(n);col+=a.rgb*pulse*.08;col=lerp(col,EnvR(n,v,.08)*1.05,F*.18);
float glassT=saturate(1.-abs(Eye.w-4.));
col=lerp(col,env*1.22+col*.28+float3(.45,.85,1.05)*F*.55,glassT*saturate(.28+F));
float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);
float al=saturate(.58+a.a*.32+F*.12);al=lerp(al,saturate(.62+F*.35),glassT*.75);float nw=saturate((5.-abs(i.w.y-Fog.z))/5.);al=saturate(lerp(al,max(al,.90),nw));
if(Dof.w>0.5){col+=a.rgb*(.08+.10*pulse)+env*F*.12;al=saturate(al+.06);}
return float4(lerp(col,float3(.55,.72,.95),fg*.55),al);}
D VSS(V x){D o;o.w=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;o.p=mul(float4(x.p,1),VP);return o;}
float3 WavP(float3 p){float t=Misc.w;float3 o=p;
float2 d1=float2(.64,.77);float ph1=.085*dot(d1,p.xz)-1.18*t;o.xz+=.082*d1*cos(ph1);o.y+=.15*sin(ph1);
float2 d2=float2(-.82,.57);float ph2=.13*dot(d2,p.xz)-1.62*t;o.xz+=.04*d2*cos(ph2);o.y+=.08*sin(ph2);
float2 d3=float2(.35,-.94);float ph3=.21*dot(d3,p.xz)-2.2*t;o.xz+=.02*d3*cos(ph3);o.y+=.045*sin(ph3);return o;}
D VSW(V x){float3 p=WavP(x.p);float3 tX=WavP(x.p+float3(.4,0,0))-WavP(x.p-float3(.4,0,0));float3 tZ=WavP(x.p+float3(0,0,.4))-WavP(x.p-float3(0,0,.4));
float3 n=normalize(cross(tZ,tX));if(n.y<0)n=-n;D o;o.w=p;o.n=n;o.uv=x.uv;o.c=x.c;o.p=mul(float4(p,1),VP);return o;}
D VSSI(V x,float4 iw:TEXCOORD2,float4 isc:TEXCOORD3,float4 ic:TEXCOORD4,float4 iex:TEXCOORD5,float4 sk:TEXCOORD6){
float3 lp=x.p;float3 ln=x.n;uint i0=min((uint)sk.x,15);uint i1=min((uint)sk.z,15);
float amp=((sk.x>=0.5&&sk.x<4.)?(0.22+0.78*saturate(iex.y)):1);
float w0=saturate(sk.y)*amp;float w1=saturate(sk.w)*amp;float wI=saturate(1-w0-w1);
float4 hp=float4(lp,1);lp=mul(hp,Bones[0]).xyz*wI+mul(hp,Bones[i0]).xyz*w0+mul(hp,Bones[i1]).xyz*w1;
ln=normalize(mul(ln,(float3x3)Bones[0])*wI+mul(ln,(float3x3)Bones[i0])*w0+mul(ln,(float3x3)Bones[i1])*w1);
float cy=cos(iw.w),sy=sin(iw.w),cp=cos(isc.w),sp=sin(isc.w),cr=cos(iex.x),sr=sin(iex.x);float3 p=lp*isc.xyz;
float x1=p.x*cr-p.y*sr,y1=p.x*sr+p.y*cr;p.x=x1;p.y=y1;
float y2=p.y*cp-p.z*sp,z2=p.y*sp+p.z*cp;float3 w=float3(iw.x+p.x*cy+z2*sy,iw.y+y2,iw.z-p.x*sy+z2*cy);
float nx1=ln.x*cr-ln.y*sr,ny1=ln.x*sr+ln.y*cr;float ny2=ny1*cp-ln.z*sp,nz2=ny1*sp+ln.z*cp;
float3 n=float3(nx1*cy+nz2*sy,ny2,-nx1*sy+nz2*cy);
float hgt=saturate(x.p.y*0.17);float fl=sin(Misc.w*1.62+iw.x*0.07+iw.z*0.055+sk.x)*Wind.w*hgt*hgt;
w.xz+=Wind.xz*fl*0.40;w.y+=fl*Wind.y*0.08;
D o;o.w=w;o.n=n;o.uv=x.uv;o.c=x.c*ic;o.c.a=x.c.a*ic.a+(iex.z>0.5?2.:0);o.p=mul(float4(w,1),VP);return o;}
struct HV{float2 p:POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};struct HO{float4 p:SV_POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};
HO VSH(HV x){HO o;o.p=float4(x.p,0,1);o.uv=x.uv;o.c=x.c;return o;}
float4 PSH(HO i):SV_Target{if(i.uv.x<-0.5)return i.c;float4 t=T0.Sample(SL,i.uv);return float4(t.rgb*i.c.rgb,t.a*i.c.a);}
float4 PSLINE(HO i):SV_Target{float t=saturate(1.-abs(i.uv.y-.5)*2.4);float cap=saturate(min(i.uv.x,1.-i.uv.x)*10.);return float4(i.c.rgb,i.c.a*t*cap);}
float4 PSS(D i):SV_Target{if(Fog.w>0.5&&i.w.y<Fog.z+0.08)discard;float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);
float nz=fbm(i.w.xz*12.+i.w.y*10.+Misc.w*.02)*0.15;
n=normalize(n+float3(nz,nz*0.5,nz)*0.8);
float3 v=normalize(Eye.xyz-i.w);float ndl=saturate(dot(n,l));float wrap=saturate(dot(n,l)*.5+.5);
float nd=lerp(.30,max(ndl,.22),sh);nd=saturate(nd*.62+wrap*wrap*.48);
float uvk=saturate((0.96-i.c.a)*18.);
float2 suv=lerp(i.w.xz*0.0048+i.w.y*0.002,i.uv,uvk);suv=lerp(suv,POM(suv,v,n,.018),1.-uvk);
float4 tex4=T0.Sample(SL,suv);float3 tex=tex4.rgb;
float3 det=T1.Sample(SL,lerp(i.w.xz*0.021+i.w.y*0.014,i.uv*2.,uvk)).rgb;
float3 photo=saturate(tex*lerp(float3(1,1,1),det*1.38,0.52*(1.-uvk)));
float3 base=lerp(i.c.rgb,photo,lerp(0.62,0.94,uvk));
float3 env=EnvR(n,v,lerp(.45,.18,uvk));float fr=pow(1.-saturate(dot(n,v)),2.2);float ndc=lerp(nd,saturate(.70+wrap*.32),uvk);
float3 c=base*ndc*ContAO(n)+env*(.14+fr*.30)*(1.-uvk*.55)+SpecB(n,l,v,lerp(.55,.28,uvk),lerp(float3(.04,.04,.04),float3(.08,.08,.08),uvk))*sh*1.8;
float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);
float al=saturate(lerp(i.c.a,max(tex4.a,0.90),uvk));if(al<0.08&&uvk>0.5)discard;
float nw=saturate((Fog.z+1.4-i.w.y)/2.6)*(1.-uvk);c=lerp(c,c*float3(.78,.88,.92)+float3(.08,.14,.16)*pow(saturate(dot(reflect(-l,n),v)),28)*sh,nw*.5);
float pe=PeelAmt(i.w);if(Peel.w>=0){if(pe>0.16)discard;return float4(lerp(c,float3(.55,.7,.92),fg*.45),al);}
if(pe<0.16)discard;return float4(lerp(c,float3(.55,.7,.92),fg*.45),al*(1.-pe*0.86));}
float4 PSCLoud(D i):SV_Target{
float4 t0=T0.Sample(SL,i.uv);float2 j=float2(.018,-.014)*(0.55+0.45*sin(Misc.w*.7+i.uv.x*6.));
float4 t1=T0.Sample(SL,saturate(i.uv+j));float dens=saturate(max(t0.a,t1.a*0.85)*i.c.a);if(dens<0.08)discard;
float3 albedo=saturate((t0.rgb*0.72+t1.rgb*0.28)*i.c.rgb);
float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float3 v=normalize(Eye.xyz-i.w);
float nl=saturate(dot(n,l)*.55+.45);float nv=saturate(dot(n,v));
float thin=dens*dens*(1.15-dens);float powder=pow(thin,1.35);
float silver=pow(saturate(1.-nv),2.4)*pow(saturate(dot(v,l)*.5+.5),3.5)*dens;
float3 sunC=float3(1.05,.93,.78);float3 skyC=float3(.52,.68,.95);
float3 amb=lerp(skyC,sunC,nl*.55+.25);
float3 c=albedo*amb*(.55+.55*nl);
c+=sunC*powder*.85*(.4+nl*.6);c+=sunC*silver*.55;
c*=lerp(float3(.88,.92,1.06),float3(1.06,.98,.90),saturate((i.w.y-Eye.y)*.015+.5));
float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);
float al=saturate(dens*0.88);return float4(lerp(c,float3(.55,.7,.92),fg*.32),al);}
float4 PlanarMir(float3 w){float4 rp=mul(float4(w,1),ReflectVP);float iw=max(rp.w,1e-5);float2 muv=rp.xy/iw*float2(.5,-.5)+.5;
float mb=(rp.w>0)*saturate(min(min(muv.x,1.-muv.x),min(muv.y,1.-muv.y))*8.);
return float4(ReflectMap.Sample(SL,saturate(muv)).rgb,mb);}
float4 PSW(D i):SV_Target{float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);
float2 wp=i.w.xz*0.065+Misc.w*float2(.025,-.02);float w1=fbm(wp),w2=fbm(wp.yx*1.5+float2(Misc.w*.045,0));
n=normalize(n+float3((w1-.5)*1.2,0,(w2-.5)*1.15)*.5);
float sh=ShadowAt(i.w,n);float3 v=normalize(Eye.xyz-i.w);float ndv=saturate(dot(n,v));float F=0.02+0.98*pow(1.-ndv,5.);
float depth=saturate(i.c.a);float3 shallow=saturate(i.c.rgb*float3(1.15,1.28,1.2));float3 deepC=float3(.02,.09,.14);
float3 waterAlb=lerp(shallow,deepC,smoothstep(.05,.8,depth));
float2 suv=i.p.xy*Screen.zw;float2 ruv=saturate(suv+n.xz*lerp(.008,.035,depth)*(1.05-ndv));
float3 refr=Depth.Sample(SL,ruv).rgb;float hasR=saturate(dot(refr,1)*2.2);
float3 beer=exp(-float3(.18,.08,.05)*lerp(.8,3.2,depth));float3 under=lerp(waterAlb,refr*beer*1.4,hasR);
float3 env=Env.Sample(SL,reflect(-v,n)).rgb;
float4 mir=PlanarMir(i.w+float3(n.x,0,n.z)*7.);
float4 mirB=PlanarMir(i.w+float3(n.z,-.04,-n.x)*3.4);if(mirB.a>mir.a*.8)mir=lerp(mir,mirB,.4);
float3 c=under*(.42+.42*saturate(dot(n,l)*.5+.5)*lerp(.6,1.,sh));
c=lerp(c,env,F*.62);c=lerp(c,mir.rgb,mir.a*saturate(.14+F*.9));
float gl=pow(saturate(dot(reflect(-l,n),v)),108)*sh;c+=float3(.82,.94,1)*gl*(.15+F*.5);
float foam=saturate(1.-depth*3.2)*smoothstep(.2,.85,w1)*(.45+.55*saturate(F+.2));
c=lerp(c,float3(.88,.93,.97),foam);
float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);
float al=lerp(lerp(.52,.97,hasR),1.,saturate(depth*.35+F*.22+foam));
return float4(lerp(c,float3(.42,.60,.80),fg*.26),saturate(al));}
float4 PST(D i):SV_Target{if(Fog.w>0.5&&i.w.y<Fog.z+0.08)discard;float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);
float slope=saturate(1.-n.y);float h=i.w.y;float th=Eye.w;
float nLo=fbm(i.w.xz*0.028), nHi=fbm(i.w.xz*0.11);
n=normalize(n+float3(nHi-.45,0,nLo-.45)*slope*0.62);
float3 v=normalize(Eye.xyz-i.w);float ndl=saturate(dot(n,l));float wrap=saturate(dot(n,l)*.5+.5);
float nd=lerp(.24,max(ndl,.16),sh);nd=saturate(nd*.58+wrap*wrap*.52);
float2 tuv=POM(i.w.xz*0.0038,v,n,.022);
float3 tex=T0.Sample(SL,tuv).rgb;
float3 det=T1.Sample(SL,i.w.xz*0.018).rgb;
float3 photo=saturate(tex*lerp(float3(1,1,1),det*1.32,0.55));
float3 dirt=lerp(i.c.rgb,photo,0.72);
float3 rock=dirt*lerp(float3(.62,.58,.52),float3(.48,.42,.36),saturate(slope*1.4));
float3 wet=lerp(dirt,float3(.16,.26,.34),0.72);
if(th<0.5){rock=float3(.36,.30,.20);wet=float3(.10,.22,.16);}
else if(th<1.5){rock=float3(.50,.46,.40);wet=float3(.28,.26,.24);}
else if(th<2.5){rock=float3(.30,.32,.34);wet=float3(.14,.16,.18);}
else if(th<3.5){rock=float3(.22,.24,.34);wet=float3(.08,.10,.18);}
else if(th<4.5){rock=float3(.20,.38,.48);wet=float3(.08,.22,.36);}
else if(th<5.5){rock=float3(.42,.36,.22);wet=float3(.12,.28,.18);}
else if(th<6.5){rock=float3(.62,.38,.24);wet=float3(.28,.18,.12);}
else {rock=float3(.78,.80,.88);wet=float3(.55,.62,.78);}
float3 base=lerp(dirt,rock,saturate(slope*2.5+nHi*0.18));
float waterL=Fog.z;float low=saturate((waterL+1.6-h)/2.2);float submerged=saturate((waterL-h)/3.5);
base=lerp(base,wet,low*(1.-slope)*0.72);
base=lerp(base,lerp(wet,float3(.05,.12,.16),.62),submerged);
if(th>6.5) base=lerp(base,float3(.93,.95,1),saturate((h-24.)/18.)*0.55);
if(th>2.5&&th<3.6) base=lerp(base,float3(.88,.92,1),saturate(n.y+.15)*.42);
if(th>5.5&&th<6.6) base=lerp(base,float3(.95,.72,.48),saturate((h-28.)/22.)*0.35);
float3 env=EnvR(n,v,lerp(.62,.28,slope));float fr=pow(1.-saturate(dot(n,v)),2.4);
float hemi=saturate(n.y*.5+.5);float3 hemiC=lerp(float3(.55,.48,.42),float3(.72,.82,.98),hemi);
float3 c=base*nd*lerp(float3(1,1,1),hemiC,0.24)*ContAO(n)+env*(.12+fr*.28)+SpecB(n,l,v,lerp(.7,.32,slope),float3(.04,.04,.04))*sh*1.6;
float specW=saturate(low*(1.-slope)*0.65);c+=float3(.25,.4,.5)*SpecB(n,l,v,.18,float3(.02,.04,.05))*specW*sh;
float grass=saturate(n.y-slope)*saturate(1.-th)*smoothstep(.35,.8,nHi);
c+=base*float3(.12,.28,.06)*pow(saturate(dot(n,l)*.4+.6),1.4)*grass*.35;
if(submerged>0.04){float2 cu=i.w.xz*.09+Misc.w*float2(.05,-.04);float cau=pow(saturate(fbm(cu)*fbm(cu.yx+.31)),2.4);c+=float3(.28,.5,.42)*cau*submerged*sh*saturate(n.y);}
float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);
float hFog=saturate((Fog.z+18.-h)/28.);c=lerp(c,float3(.62,.74,.92),hFog*fg*.25);
float pe=PeelAmt(i.w);if(Peel.w>=0){if(pe>0.16)discard;return float4(lerp(c,float3(.55,.7,.92),fg*.42),1);}
if(pe<0.16)discard;return float4(lerp(c,float3(.55,.7,.92),fg*.42),saturate(1.-pe*0.86));}
float4 PSC(D i):SV_Target{if(Fog.w>0.5&&i.w.y<Fog.z+0.08)discard;float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);
float3 v=normalize(Eye.xyz-i.w);
float ply=saturate((i.c.a-1.45)*4.);float va=i.c.a-ply*2.;
float3 albedo=T0.Sample(SL,i.uv).rgb;
float3 det=T1.Sample(SL,i.uv*4.6).rgb;
albedo=saturate(albedo*lerp(float3(1,1,1),det*1.22,0.34));
float nz=(albedo.r-0.5)*0.14 + fbm(i.w.xz*12.+i.w.y*10.+Misc.w*.02)*0.08;
n=normalize(n+float3(nz,nz,nz));
float nd=lerp(.42,max(saturate(dot(n,l)),.28),sh);nd=saturate(nd*.70+saturate(dot(n,l)*.5+.5)*saturate(dot(n,l)*.5+.5)*.32);
float3 base=saturate(i.c.rgb*albedo*1.05);
float glass=saturate((va-0.74)*10.)*saturate((0.90-va)*16.);
float rough=lerp(.40,.08,glass);float3 F0=lerp(float3(.045,.048,.052),float3(.16,.20,.26),glass);
float3 env=EnvR(n,v,rough);float fr=pow(1.-saturate(dot(n,v)),3.1);
float3 c=base*(0.40+0.60*nd)*ContAO(n)+env*(.07+fr*(.12+.28*glass))+SpecB(n,l,v,rough,F0)*sh*lerp(1.05,2.05,glass);
c=lerp(c,c*float3(.16,.17,.22),ply*fr*0.62);
c+=base*ply*(1.-fr)*0.05;
if(i.w.y<Fog.z){float uw=saturate((Fog.z-i.w.y)*.15);c=lerp(c,c*float3(.42,.7,.86),uw*.62);}
float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);
return float4(lerp(c,float3(.55,.7,.92),fg*.3),saturate(lerp(va,0.78,glass*.40)));}
struct Q{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};Q VSQ(uint id:SV_VertexID){Q o;float2 p=float2((id==2)?3:-1,(id==1)?3:-1);o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,(1-p.y)*.5);return o;}
float4 SSR(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);float z=Depth.Sample(SP,i.uv).r;
float4 fx=NoiseMap.Sample(SL,i.uv+float2(0,frac(Misc.w*.18)));float th=Eye.w;
float2 oc=i.uv+float2(.0016,-.0024);float cs=saturate((Depth.Sample(SP,oc).r-z)*62.);c.rgb*=lerp(.66,1.,1.-cs*.48);
float ao=1;const float2 aoO[6]={float2(.004,.002),float2(-.0035,.003),float2(.0025,-.004),float2(-.004,-.002),float2(.0055,0),float2(0,.005)};
[unroll]for(int a=0;a<6;a++){float zd=Depth.Sample(SP,saturate(i.uv+aoO[a])).r;ao-=saturate((z-zd)*22.)*0.07;}c.rgb*=lerp(.72,1.,saturate(ao));
float2 rp=i.uv;float2 rd=float2((i.uv.x-.5)*.028,.016);float3 rc=c.rgb;float rk=0;
[unroll]for(int s=0;s<16;s++){rp+=rd;if(any(rp<0)||any(rp>1))break;float dz=Depth.Sample(SP,rp).r;if(dz<z-0.0014){rc=T0.Sample(SL,rp).rgb;rk=exp(-s*.11);break;}}
c.rgb=lerp(c.rgb,rc,rk*0.36*smoothstep(.10,.88,z));
float3 ld=normalize(LightDir.xyz);float2 sun=saturate(float2(.5,.2)+float2(ld.x,-ld.y)*.34);float2 dir=sun-i.uv;float rays=0;float2 p=i.uv;
[unroll]for(int k=0;k<14;k++){p+=dir*.02;if(any(p<0)||any(p>1))break;rays+=saturate(.14-Depth.Sample(SP,p).r)*exp(-k*.13);}
c.rgb+=(th>3.5&&th<4.6?float3(.45,.8,1):float3(1,.92,.7))*rays*(th>3.5&&th<4.6?.14:.26);
if(th>6.4||(th>2.5&&th<3.6)){float s=fx.b*smoothstep(.1,.92,1.-z);c.rgb=lerp(c.rgb,float3(.92,.96,1),s*.16);
c.rgb+=float3(.8,.88,1)*fx.b*fx.g*.10;float2 st=i.uv+float2(fx.r*.05-.025,-frac(Misc.w*1.15+i.uv.x*9.)*.09)+Wind.xz*.002;
c.rgb+=NoiseMap.Sample(SL,st).b*float3(.85,.9,1)*smoothstep(.18,1.,1.-z)*.08;}
if(th>3.5&&th<4.6){float g=fx.a;c.rgb=lerp(c.rgb,c.rgb*float3(.68,1.08,1.24)+fx.rgb*.14,g*.14);
c.rgb+=float3(.32,.7,1)*pow(saturate(1.-z),2.)*g*.06;}
float3 bl=T0.Sample(SL,i.uv+Screen.zw*5.).rgb+T0.Sample(SL,i.uv-Screen.zw*5.).rgb;c.rgb=lerp(c.rgb,max(c.rgb,bl*.5),.07);
return float4(c.rgb,1);}
float4 DOFP(Q i):SV_Target{float zd=Depth.Sample(SP,i.uv).r;const float zn=.08,zf=220.;
float eyeZ=zn*zf/max(1e-4,zf-zd*(zf-zn));float coc=saturate((eyeZ-Dof.x)/max(.05,Dof.y));coc=coc*coc*(3.-2.*coc);
float b=coc*Dof.z*(1.+Dof.w);if(b<1.15)return T0.Sample(SL,i.uv);float2 px=float2(b,b)*Screen.zw;
float4 c=T0.Sample(SL,i.uv)*.28;c+=(T0.Sample(SL,i.uv+float2(px.x,0))+T0.Sample(SL,i.uv-float2(px.x,0))+T0.Sample(SL,i.uv+float2(0,px.y))+T0.Sample(SL,i.uv-float2(0,px.y)))*.13;
c+=(T0.Sample(SL,i.uv+px)+T0.Sample(SL,i.uv-px)+T0.Sample(SL,i.uv+float2(px.x,-px.y))+T0.Sample(SL,i.uv+float2(-px.x,px.y)))*.05;return c;}
float4 FIN(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);if(Misc.z>8.f){c.a*=saturate(Misc.y);return c;}
float v=saturate(1-dot((i.uv-.5)*1.1,(i.uv-.5)*1.1));c.rgb*=lerp(.86,1.08,v);
float th=Eye.w;float3 tone=float3(1.02,.98,.96);if(th>6.5)tone=float3(1.05,.92,.85);else if(th>5.5)tone=float3(.9,1.02,1.08);
else if(th>4.5)tone=float3(.88,.92,1.08);else if(th>3.5)tone=float3(.95,.95,1.02);else if(th>2.5)tone=float3(1.0,.9,.85);
else if(th>1.5)tone=float3(.95,1.02,.92);c.rgb=saturate(c.rgb*tone);
float3 x=max(c.rgb,0);c.rgb=saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));
float lum=dot(c.rgb,float3(.299,.587,.114));c.rgb+=c.rgb*saturate(lum-.90)*.08;
c.rgb=lerp(c.rgb,c.rgb*c.rgb*(3.-2.*c.rgb),0.06);
if(Misc.x>0.01)c.rgb=lerp(c.rgb,1,Misc.x*.35);
float cas=saturate(Dof.w);if(cas>0.01){float2 px=Screen.zw*1.25f;float3 soft=T0.Sample(SL,i.uv+float2(px.x,0)).rgb+T0.Sample(SL,i.uv-float2(px.x,0)).rgb+T0.Sample(SL,i.uv+float2(0,px.y)).rgb+T0.Sample(SL,i.uv-float2(0,px.y)).rgb;soft*=.25;c.rgb=saturate(c.rgb+(c.rgb-soft)*cas);}
float4 nx=NoiseMap.Sample(SL,i.uv*float2(2.2,1.7)+float2(Misc.w*.28,-Misc.w*.52));
if(th>6.4||(th>2.5&&th<3.6)){float snow=smoothstep(.42,1.,nx.b)*smoothstep(.08,.95,i.uv.y);c.rgb=lerp(c.rgb,float3(.93,.96,1),snow*.14);c.rgb+=float3(.82,.9,1)*nx.g*nx.b*.07;}
if(th>3.5&&th<4.6)c.rgb=lerp(c.rgb,c.rgb*float3(.72,1.06,1.22)+float3(.18,.42,.55),nx.a*.10);
float2 ca=Screen.zw*(0.85+1.6*length(i.uv-.5));c.r=lerp(c.r,T0.Sample(SL,saturate(i.uv+ca)).r,.08);c.b=lerp(c.b,T0.Sample(SL,saturate(i.uv-ca)).b,.08);
c.rgb+=(nx.r-.5)*0.02;
if(Eye.y<Fog.z){float uw=saturate((Fog.z-Eye.y)*.1);c.rgb*=lerp(1,float3(.48,.74,.9),uw*.72);
float cau=pow(saturate(nx.g*nx.b),1.7);c.rgb+=float3(.18,.42,.38)*cau*uw*.2;c.rgb=lerp(c.rgb,float3(.07,.16,.2),uw*.2);}
return c;}
RWTexture2D<float4> NoiseOut:register(u0);
[numthreads(8,8,1)]void CSNoise(uint3 id:SV_DispatchThreadID){
uint2 p=id.xy;if(p.x>=256||p.y>=256)return;float2 uv=(float2(p)+.5)/256.;
float n=fbm(uv*8.+Misc.w*.22);float n2=fbm(uv*22.-Misc.w*.48);
float snow=smoothstep(.48,1.,frac(uv.x*48.+uv.y*14.-Misc.w*1.55+n*2.));
float frost=saturate(n*n2*1.05+pow(saturate(1.-length(uv-.5)*1.6),2.)*.08);
NoiseOut[p]=float4(n,n2,snow,frost);}
