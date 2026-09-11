/* ============================================================
 * host-detail.js — 上位机发布详情页共享渲染器
 * 数据源: 站点根 releases.json (单一事实来源, 见 SPEC.md §3/§4)
 * 壳页只需: window.MDC_HOST_DETAIL = '<上位机版本>'; 然后引入本文件
 * ============================================================ */
(function(){
  'use strict';
  var HV = window.MDC_HOST_DETAIL;

  /* ====== 主题: URL ?theme= > localStorage > 系统偏好 > 默认深色 (与发布页一致) ====== */
  try{
    var p=new URLSearchParams(location.search).get('theme');
    var t=(p==='light'||p==='dark')?p:(localStorage.getItem('mdc-theme')||(matchMedia('(prefers-color-scheme: light)').matches?'light':'dark'));
    document.documentElement.dataset.theme=t;
  }catch(e){}

  /* ====== releases.json 基址 = 本脚本所在目录 (壳页位于 host/ 子目录也能正确定位站点根)
     注: currentScript 仅在同步执行期有效, 必须在此处捕获 ====== */
  var script=document.currentScript||(function(){var s=document.getElementsByTagName('script');return s[s.length-1]})();
  var base='';
  try{base=script.src.replace(/[^/]*$/,'');}catch(e){base='./';}

  /* 壳页在 <head> 中同步引入本文件, 此时 document.body 尚不存在 — 渲染挂到 DOM 就绪后 */
  if(document.readyState==='loading'){
    document.addEventListener('DOMContentLoaded', boot);
  }else{
    boot();
  }

  function boot(){
  /* ====== 样式 (与发布页/固件详情页同一套设计 token) ====== */
  var css=[
    ':root,html[data-theme="dark"]{--bg:#0a0e18;--panel:#111827;--panel2:#0d1321;--text:#c5c9d4;--border:#1f2937;',
    '--accent:#4b8ce8;--accent2:#5ea0f5;--green:#3a9a56;--green2:#47b06a;--red:#d94444;',
    '--muted:#8b93a3;--gold:#e8b84b;--purple:#a78bfa;--glow:rgba(75,140,232,.14)}',
    'html[data-theme="light"]{--bg:#f4f6fa;--panel:#ffffff;--panel2:#eef1f6;--text:#2a3140;--border:#d9dfe8;',
    '--accent:#2563eb;--accent2:#3b82f6;--green:#16a34a;--green2:#22c55e;--red:#dc2626;',
    '--muted:#64748b;--gold:#b45309;--purple:#7c3aed;--glow:rgba(37,99,235,.08)}',
    '*{margin:0;padding:0;box-sizing:border-box}',
    'body{font:15px/1.7 \'Segoe UI\',\'PingFang SC\',\'Microsoft YaHei\',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;transition:background .25s,color .25s;-webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale;text-rendering:optimizeLegibility}',
    '.wrap{max-width:760px;margin:0 auto;padding:0 20px 56px}',
    'a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}',
    '.back{display:inline-flex;align-items:center;gap:6px;margin:26px 0 4px;font-size:13px;color:var(--muted)}',
    '.back:hover{color:var(--accent);text-decoration:none}',
    'h1{font-size:26px;margin:6px 0 2px;color:var(--text)}',
    'h1 .fwv{font-family:Consolas,monospace}',
    '.hero{padding:8px 0 20px;border-bottom:1px solid var(--border);margin-bottom:22px;',
    'background:radial-gradient(ellipse 80% 60% at 50% -10%,var(--glow),transparent)}',
    '.hero .meta{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:10px}',
    '.chip{display:inline-flex;align-items:center;gap:5px;padding:1px 10px;border-radius:12px;font-size:12.5px;border:1px solid var(--border);color:var(--muted)}',
    '.chip.acc{color:var(--accent);border-color:color-mix(in srgb,var(--accent) 40%,transparent)}',
    '.chip.grn{color:var(--green);border-color:color-mix(in srgb,var(--green) 50%,transparent)}',
    '.chip.gld{color:var(--gold);border-color:color-mix(in srgb,var(--gold) 40%,transparent)}',
    'section{margin-bottom:36px}section+section{border-top:1px solid var(--border);padding-top:36px}',
    'h2{font-size:16.5px;color:var(--text);margin-bottom:14px;display:flex;align-items:center;gap:9px}',
    '.ic{width:16px;height:16px;flex-shrink:0;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round;vertical-align:-3px}',
    '.ic.fill{fill:currentColor;stroke:none}',
    'h2 .ic{width:17px;height:17px}h2 .hi-acc{color:var(--accent)}h2 .hi-gold{color:var(--gold)}',
    '.kv{display:grid;grid-template-columns:130px 1fr;gap:6px 14px;font-size:14px}',
    '.kv .k{color:var(--muted)}.kv .v{color:var(--text)}',
    '.btn{display:inline-flex;align-items:center;gap:7px;background:var(--accent);color:#fff;padding:9px 20px;border-radius:8px;',
    'font-size:14px;font-weight:600;text-decoration:none;border:none;cursor:pointer;transition:.15s}',
    '.btn:hover{background:var(--accent2);text-decoration:none;transform:translateY(-1px)}',
    '.btn.green{background:var(--green)}.btn.green:hover{background:var(--green2)}',
    '.btn.ghost{background:transparent;border:1px solid var(--border);color:var(--text);font-weight:500}',
    '.btn.ghost:hover{border-color:var(--accent);color:var(--accent);background:transparent;transform:none}',
    '.btn.sm{padding:5px 12px;font-size:12.5px;border-radius:6px;font-weight:500}',
    '.btns{display:flex;gap:10px;flex-wrap:wrap;align-items:center}',
    '.lb{margin-top:16px}',
    '.lh{display:flex;align-items:center;gap:8px;font-size:12px;font-weight:600;letter-spacing:.5px;color:var(--muted);margin-bottom:2px}',
    '.lhead,.lr{display:grid;gap:12px;align-items:center;padding:8px 2px;border-bottom:1px solid var(--hair)}',
    ':root,html[data-theme="dark"]{--hair:color-mix(in srgb,var(--border) 72%,transparent)}html[data-theme="light"]{--hair:color-mix(in srgb,var(--border) 72%,transparent)}',
    '.lhead{font-size:12px;color:var(--muted);padding:4px 2px;letter-spacing:.4px}',
    '.lr{transition:background .15s}.lr:hover{background:color-mix(in srgb,var(--accent) 4%,transparent)}.lr:last-child{border-bottom:none}',
    '.lg-files{grid-template-columns:24px minmax(0,1.15fr) minmax(0,1.6fr) 92px 40px}',
    '.fticon{width:16px;height:15px;color:var(--accent)}',
    '.fn{font-family:Consolas,monospace;font-size:13.5px;color:var(--text);word-break:break-all}',
    '.fn a{color:var(--text)}.fn a:hover{color:var(--accent);text-decoration:none}',
    '.fnote{font-size:12.5px;color:var(--muted)}',
    '.fdate{font-size:12.5px;color:var(--muted);font-family:Consolas,monospace;white-space:nowrap}',
    '.c-op{text-align:right}',
    '.ibtn{display:inline-flex;align-items:center;gap:5px;padding:3px 6px;border-radius:6px;font-size:12px;',
    'color:var(--accent);text-decoration:none;flex-shrink:0}',
    '.ibtn .ic{width:13px;height:13px}',
    '.ibtn:hover{background:color-mix(in srgb,var(--accent) 12%,transparent);text-decoration:none}',
    '.subhd{font-size:13px;color:var(--muted);font-weight:600;letter-spacing:.5px;margin:14px 0 8px}',
    'ul.plain{list-style:none}ul.plain li{margin:6px 0;font-size:14px;display:flex;gap:9px;align-items:baseline}',
    'ul.plain li .ic{width:13px;height:13px;flex-shrink:0;transform:translateY(2px)}',
    'ul.plain li .rnote{color:var(--muted);font-size:12.5px}',
    '.note{background:var(--panel2);border:1px solid var(--border);border-radius:8px;padding:12px 16px;font-size:14px;color:var(--muted);margin-top:8px}',
    '.note b{color:var(--text)}',
    'ol{padding-left:22px;color:var(--muted);font-size:14px}ol li{margin-bottom:6px}ol b{color:var(--text)}',
    '.err{text-align:center;color:var(--muted);padding:60px 0;font-size:14px}',
    '@media(max-width:560px){.kv{grid-template-columns:110px 1fr}h1{font-size:21px}.lhead{display:none}.lg-files{display:flex;flex-wrap:wrap}.fnote{flex:1;min-width:150px}.fdate{margin-left:auto}.c-op{width:100%;text-align:left}}'
  ].join('');
  var st=document.createElement('style');st.textContent=css;document.head.appendChild(st);

  /* ====== 工具 ====== */
  function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]})}
  function ic(n,cls){return '<svg class="ic '+(cls||'')+'"><use href="#i-'+n+'"/></svg>'}
  function dateOrDash(d){return d?esc(d):'—'}
  function btn(href,label,cls,title){
    if(!href)return '';
    return '<a class="btn '+(cls||'ghost')+'" href="'+esc(href)+'" download'+(title?' title="'+esc(title)+'"':'')+'>'+ic('download')+esc(label)+'</a>';
  }
  function errBox(msg){
    document.body.innerHTML='<div class="wrap"><div class="err">'+esc(msg)+'<br><br>'+
      '<a class="btn sm ghost" href="'+base+'index.html">← 返回发布页</a></div></div>';
  }

  /* 图标库 (与发布页同款, 本页内联一份) */
  document.body.insertAdjacentHTML('beforebegin',
    '<svg style="display:none" aria-hidden="true">'+
    '<symbol id="i-monitor" viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8m-4-4v4"/></symbol>'+
    '<symbol id="i-download" viewBox="0 0 24 24"><path d="M12 3v11m0 0 4-4m-4 4-4-4"/><path d="M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/></symbol>'+
    '<symbol id="i-book" viewBox="0 0 24 24"><path d="M2 4h6a4 4 0 0 1 4 4v12a3 3 0 0 0-3-3H2zM22 4h-6a4 4 0 0 0-4 4v12a3 3 0 0 1 3-3h7z"/></symbol>'+
    '<symbol id="i-file" viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6M9 13h6M9 17h6"/></symbol>'+
    '<symbol id="i-folder" viewBox="0 0 24 24"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/></symbol>'+
    '<symbol id="i-package" viewBox="0 0 24 24"><path d="M21 8 12 3 3 8v8l9 5 9-5z"/><path d="M3 8l9 5 9-5M12 13v8"/></symbol>'+
    '<symbol id="i-check" viewBox="0 0 24 24"><path d="M20 6 9 17l-5-5"/></symbol>'+
    '<symbol id="i-bolt" viewBox="0 0 24 24"><path d="M13 2 3.5 13.5h6L10 22l10.5-11.5h-6z"/></symbol>'+
    '<symbol id="i-f-bin" viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/><text x="11.5" y="17.5" text-anchor="middle" font-family="Consolas,\'Courier New\',monospace" font-weight="700" font-size="7" fill="currentColor" stroke="none">BIN</text></symbol>'+
    '<symbol id="i-f-hex" viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/><text x="11.5" y="17.5" text-anchor="middle" font-family="Consolas,\'Courier New\',monospace" font-weight="700" font-size="7" fill="currentColor" stroke="none">HEX</text></symbol>'+
    '<symbol id="i-f-html" viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/><text x="11.5" y="17.2" text-anchor="middle" font-family="Consolas,\'Courier New\',monospace" font-weight="700" font-size="5.6" fill="currentColor" stroke="none">HTML</text></symbol>'+
    '<symbol id="i-f-pdf" viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/><text x="11.5" y="17.5" text-anchor="middle" font-family="Consolas,\'Courier New\',monospace" font-weight="700" font-size="7" fill="currentColor" stroke="none">PDF</text></symbol>'+
    '<symbol id="i-f-md" viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/><text x="11.5" y="17.5" text-anchor="middle" font-family="Consolas,\'Courier New\',monospace" font-weight="700" font-size="8" fill="currentColor" stroke="none">MD</text></symbol>'+
    '<symbol id="i-f-zip" viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/><text x="11.5" y="17.5" text-anchor="middle" font-family="Consolas,\'Courier New\',monospace" font-weight="700" font-size="7" fill="currentColor" stroke="none">ZIP</text></symbol>'+
    '</svg>');

  if(!HV){errBox('壳页缺少版本声明: 需在引入 host-detail.js 前设置 window.MDC_HOST_DETAIL');return;}

  fetch(base+'releases.json',{cache:'no-cache'}).then(function(r){
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(function(m){
    var line=null,host=null;
    (m.lines||[]).forEach(function(l){
      (l.host_releases||[]).forEach(function(h){if(h.ver===HV){line=l;host=h;}});
    });
    if(!host){errBox('未在版本清单中找到上位机 v'+HV+' — 请确认 releases.json 已包含该版本的 detail 字段');return;}

    var cur=!!line.current;
    /* 配套固件 = 同线 releases 中 host.ver 指向本上位机版本的固件 */
    var fws=(line.releases||[]).filter(function(r){return r.host&&r.host.ver===HV;});
    document.title='上位机 v'+host.ver+' — Motor Driver Controller 发布详情';

    var html='<div class="wrap">'+
      '<a class="back" href="'+base+'index.html">← 返回发布页</a>'+
      '<div class="hero">'+
        '<h1>上位机 <span class="fwv">v'+esc(host.ver)+'</span></h1>'+
        '<div class="meta">'+
          '<span class="chip acc">'+esc(line.name)+'</span>'+
          '<span class="chip">协议 D='+line.protocol+'</span>'+
          (cur?'<span class="chip grn">当前维护</span>':'<span class="chip gld">存档 · 不再更新</span>')+
          '<span class="chip">发布 '+dateOrDash(host.date)+'</span>'+
        '</div>'+
      '</div>';

    /* 1. 版本信息 */
    html+='<section><h2>'+ic('file','hi-acc')+'版本信息</h2><div class="kv">'+
      '<span class="k">上位机版本</span><span class="v">v'+esc(host.ver)+'（vD.F · D=协议 F=修复）</span>'+
      '<span class="k">所属线</span><span class="v">'+esc(line.name)+' · 协议 D='+line.protocol+'</span>'+
      '<span class="k">发布日期</span><span class="v">'+dateOrDash(host.date)+'</span>'+
      '<span class="k">配套固件</span><span class="v">'+(fws.length?fws.map(function(r){return 'v'+esc(r.fw);}).join(' / '):'—')+'</span>'+
      '<span class="k">在线入口</span><span class="v">'+(host.web?'<a href="'+esc(host.web)+'">'+esc(host.web)+'</a>':'—')+'</span>'+
      '<span class="k">状态</span><span class="v">'+(cur?'当前维护':'存档 · 不再更新')+'</span>'+
      '</div></section>';

    /* 2. 更新内容 */
    html+='<section><h2>'+ic('check','hi-acc')+'更新内容</h2>';
    if(host.notes){
      html+='<ul class="plain"><li>'+ic('check')+'<span>'+esc(host.notes)+'</span></li></ul>';
    }else html+='<div class="note">暂无更新说明</div>';
    html+='</section>';

    /* 3. 下载 (统一文件列表行格式 + 表头, 与首页一致) */
    function fti(href){
      var h=String(href||'').toLowerCase();
      if(/\.html?$/.test(h))return ic('f-html','fticon');
      if(/\.bin$/.test(h))return ic('f-bin','fticon');
      if(/\.hex$/.test(h))return ic('f-hex','fticon');
      if(/\.pdf$/.test(h))return ic('f-pdf','fticon');
      if(/\.md$/.test(h))return ic('f-md','fticon');
      if(/\.zip$/.test(h))return ic('f-zip','fticon');
      return ic('file','fticon');
    }
    function frow(href,note,date){
      if(!href)return '';
      var nm;try{nm=decodeURIComponent(String(href).split('/').pop());}catch(e){nm=String(href).split('/').pop();}
      return '<div class="lr lg-files">'+fti(href)+
        '<span class="fn"><a href="'+esc(href)+'" download title="下载 '+esc(nm)+'">'+esc(nm)+'</a></span>'+
        '<span class="fnote">'+(note?esc(note):'')+'</span>'+
        '<span class="fdate">'+(date?esc(date):'—')+'</span>'+
        '<span class="c-op"><a class="ibtn" href="'+esc(href)+'" download title="下载 '+esc(nm)+'">'+ic('download')+'</a></span></div>';
    }
    function flist(label,rows){
      return '<div class="lb"><div class="lh">'+esc(label)+'</div>'+
        '<div class="lhead lg-files"><span></span><span>文件</span><span>说明</span><span>发布时间</span><span class="c-op">操作</span></div>'+
        rows.join('')+'</div>';
    }
    html+='<section><h2>'+ic('download','hi-acc')+'下载</h2>'+
      flist('上位机 v'+esc(host.ver),[
        frow(host.offline,'离线版，无检查更新与固件在线下载功能',host.date),
        frow(host.online,'联网版，含在线固件库 / 检查更新，断网自动降级',host.date)
      ])+
      (host.web?'<div class="btns" style="margin-top:10px"><a class="btn sm ghost" href="'+esc(host.web)+'">在线使用</a></div>':'');
    html+='</section>';

    /* 4. 配套固件 (同一上位机版本配套的全部固件发布, 含各自详情页入口) */
    if(fws.length){
      html+='<section><h2>'+ic('bolt','hi-gold')+'配套固件</h2>';
      fws.forEach(function(r){
        html+=flist('固件 v'+esc(r.fw),[
          frow(r.fw_bin,'固件二进制（上位机刷写用）',r.date),
          frow(r.fw_hex,'HEX 格式（烧录器直刷）',r.date)
        ]);
        if(r.detail){
          html+='<div class="btns" style="margin-top:8px"><a class="btn sm ghost" href="'+esc(r.detail)+'">固件 v'+esc(r.fw)+' 发布详情</a></div>';
        }
      });
      html+='</section>';
    }

    /* 5. 手册与资料 (线级 resources; 版本级资料见各配套固件的详情页) */
    var rs=line.resources||[];
    html+='<section><h2>'+ic('book','hi-acc')+'手册与资料</h2>';
    if(rs.length){
      html+='<ul class="plain">';
      rs.forEach(function(r){
        var ext=!!(r.href&&/^https?:/i.test(r.href));
        var folder=/github\.com\/[^/]+\/[^/]+\/tree\//.test(String(r.href||'').toLowerCase());
        html+='<li>'+ic(folder?'folder':'file')+'<span>'+
          (r.href?'<a href="'+esc(r.href)+'"'+(ext?' target="_blank" rel="noopener"':'')+'>'+esc(r.name)+'</a>':'<b>'+esc(r.name)+'</b>')+'</span>'+
          (r.note?'<span class="rnote">— '+esc(r.note)+'</span>':'')+'</li>';
      });
      html+='</ul>';
    }else html+='<div class="note">暂无资料</div>';
    html+='</section>';

    /* 6. 兼容性说明 */
    html+='<section><h2>'+ic('package','hi-acc')+'兼容性说明</h2><div class="note">'+
      '上位机与固件的<b>协议版本 D 必须一致</b>（本版本 D='+line.protocol+
      '）。版本不符时请从<a href="'+base+'index.html">发布页</a>下载配套版本；联网版上位机检测到不匹配会自动引导切换。'+
      '低版本配置升级后通用；config_t 二进制布局仅在协议版本 D 变更时才会变化。</div></section>';

    html+='</div>';
    document.body.innerHTML=html;
  }).catch(function(e){
    errBox('版本清单加载失败（'+(e&&e.message?e.message:e)+'）');
  });
  }
})();
