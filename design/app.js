'use strict';
// Offline interaction sample. No fetch, credentials, storage or actual IME registration.
const $ = (id) => document.getElementById(id);
const examples = {
  tired: {
    source: '我今天累坏了，想早点睡。',
    english: "I'm beat. I think I'll turn in early tonight.",
    items: [
      ["I'm beat", '口语短语', '我累坏了。常见的非正式口语表达，比 tired 的疲惫程度更强。'],
      ['turn in early', '短语', '早点睡。turn in 在这里表示“上床睡觉”。'],
      ["I think I'll…", '句型', '用来表达当下的打算，语气自然、轻松。', "I think I'll take a break.", '我想休息一下。']
    ]
  },
  raincheck: {
    source: '要不我们改天再约？',
    english: 'Can we take a rain check?',
    items: [
      ['take a rain check', '口语短语', '这次先推迟，改天再约。常用于委婉地延后一次邀约。'],
      ['Can we…?', '句型', '用问句提出建议，给对方留下商量的空间。', 'Can we talk about this tomorrow?', '我们明天再聊这件事，可以吗？']
    ]
  },
  reply: {
    source: '你方便的时候回我就好。',
    english: 'Just get back to me whenever you get a chance.',
    items: [
      ['get back to me', '短语', '回复我、再联系我。这里不是“回到我身边”。'],
      ['whenever you get a chance', '口语短语', '你有空的时候就好，表达不催促的态度。'],
      ['Just…', '语气', '这里接近“只要……就好”，让请求显得轻松。', 'Just let me know.', '告诉我一声就好。']
    ]
  }
};
const partials = {
  '我今天有点累。': "I'm a little tired today.",
  '我今天累坏了。': "I'm beat today.",
  '我今天累坏了，想早点休息。': "I'm beat. I could use an early night.",
  [examples.tired.source]: examples.tired.english
};
const state = {scene:'input', example:'tired', version:0, epoch:0, active:false, pending:null, readyVersion:0, result:examples.tired.english, pendingPinyin:false, error:null, timers:new Set(), lastStart:0, explanationError:false, theme:'system', density:'standard'};
let toastTimer;
const schedule = (fn, ms) => { const epoch = state.epoch; const id = setTimeout(() => { state.timers.delete(id); if (epoch === state.epoch) fn(); }, ms); state.timers.add(id); return id; };
function clearJobs() { state.epoch++; state.timers.forEach(clearTimeout); state.timers.clear(); state.active=false; state.pending=null; state.lastStart=0; }
function toast(message) { clearTimeout(toastTimer); $('toast').textContent=message; $('toast').hidden=false; toastTimer=setTimeout(()=>$('toast').hidden=true,3500); }
function setStatus(text, kind='') { $('translationStatus').textContent=text; $('translationStatus').className='translation-status'+(kind?' '+kind:''); }
function updateCount() { $('charCount').textContent=Array.from($('sentenceInput').value).length+' / 300'; }
function showNotice(message) { $('inlineNotice').textContent=message; $('inlineNotice').hidden=!message; }
function currentExample() { return Object.values(examples).find(x=>x.source===$('sentenceInput').value.trim()); }
function setLearn(open) {
  $('explanationPanel').hidden=!open;
  $('learnToggle').setAttribute('aria-expanded',String(open));
  if (open) renderExplanation();
}
function renderExplanation() {
  const host=$('explanationContent'); host.replaceChildren();
  if(state.explanationError) {
    const p=document.createElement('p'); p.className='field-hint'; p.textContent='讲解暂时不可用，当前英文仍可输入。';
    const b=document.createElement('button'); b.type='button'; b.className='text-action'; b.textContent='重试讲解';
    b.addEventListener('click',()=>{state.explanationError=false;renderExplanation();}); host.append(p,b);
    document.querySelector('.small-tag').textContent='可重试'; return;
  }
  const entry=currentExample();
  if(!entry) return;
  document.querySelector('.small-tag').textContent=entry.items.length+' 个重点';
  for(const [expression,kind,meaning,exampleEn,exampleZh] of entry.items) {
    const item=document.createElement('article'); item.className='expression-item';
    const head=document.createElement('div'); head.className='expression-head';
    const strong=document.createElement('strong'); strong.lang='en'; strong.textContent=expression;
    const type=document.createElement('span'); type.className='expression-kind';type.textContent=kind;head.append(strong,type);
    const p=document.createElement('p');p.textContent=meaning;item.append(head,p);
    if(exampleEn){const example=document.createElement('div');example.className='example-sentence';example.lang='en';example.append(document.createTextNode(exampleEn));const zh=document.createElement('span');zh.lang='zh-CN';zh.textContent=exampleZh;example.append(zh);item.append(example);}
    host.append(item);
  }
}
function updateActions() {
  const exact=state.readyVersion===state.version && !!state.result && !state.error && !state.pendingPinyin;
  $('commitChinese').disabled=!$('sentenceInput').value.trim()&&!state.pendingPinyin;
  $('commitEnglish').disabled=!exact;
  $('learnToggle').disabled=!exact||!currentExample();
}
function connected(ok=true) {
  $('connectionState').replaceChildren();const dot=document.createElement('span');dot.className='status-dot';if(!ok)dot.classList.add('muted-dot');
  $('connectionState').append(dot,document.createTextNode(ok?'已连接':'未连接'));
}
function resetExample(key='tired') {
  clearJobs();state.example=key;state.version++;state.readyVersion=state.version;state.result=examples[key].english;state.pendingPinyin=false;state.error=null;state.explanationError=false;
  $('sentenceInput').value=examples[key].source;$('englishResult').textContent=state.result;$('englishResult').className='english-result';
  $('imePanel').hidden=false;$('commitSuccess').hidden=true;$('pinyinRow').hidden=true;$('errorActions').hidden=true;$('targetText').textContent='';
  setLearn(false);showNotice('');connected();setStatus('就绪');updateCount();updateActions();
  document.querySelectorAll('[data-example]').forEach(b=>b.classList.toggle('selected',b.dataset.example===key));
}
function updateControls(mode='ready') {
  const controls=$('scenarioControls'); controls.replaceChildren();
  if(state.scene==='settings') {controls.hidden=true;return;} controls.hidden=false;
  const options=state.scene==='errors'?[['offline','网络中断'],['key','Key 无效'],['unconfigured','未配置'],['explain-error','讲解失败']]:[['ready','翻译就绪'],['pinyin','拼音选字'],['streaming','持续更新']];
  options.forEach(([value,label])=>{const b=document.createElement('button');b.type='button';b.className='chip'+(value===mode?' active':'');b.dataset.state=value;b.textContent=label;b.addEventListener('click',()=>selectState(value));controls.append(b);});
  const note=document.createElement('span');note.className='controls-note';note.textContent=state.scene==='errors'?'失败时仍可选择输入中文':'可直接修改中文，或点击示例句';controls.append(note);
}
function setScene(scene) {
  state.scene=scene;clearJobs();
  document.querySelectorAll('[data-scene]').forEach(b=>{b.classList.toggle('active',b.dataset.scene===scene);b.setAttribute('aria-pressed',String(b.dataset.scene===scene));});
  $('settingsWindow').hidden=scene!=='settings';$('hostWindow').hidden=scene==='settings';
  const titles={input:['输入与翻译','浮动输入面板','先组织，再表达','中文留在编辑区，选定后再输入到当前软件。'],learn:['词句讲解','按需展开的学习层','学习不打断表达','重点词、短语和句型，只讲这一句里值得学的部分。'],errors:['异常状态','网络不影响中文输入','故障只影响联网能力','旧译文不能误提交；中文草稿始终可编辑、可输入。'],settings:['连接与外观','独立设置窗口','每个人，使用自己的服务','正式客户端由系统凭据存储保护个人 API Key。']};
  const t=titles[scene];$('sceneTitle').textContent=t[0];$('sceneSubtitle').textContent=t[1];$('noteTitle').textContent=t[2];$('noteBody').textContent=t[3];
  if(scene!=='settings'){resetExample(state.example);if(scene==='learn')setLearn(true);}
  updateControls(scene==='errors'?'offline':'ready');if(scene==='errors')showError('offline');
}
function startSettings() {
  setScene('settings');
}
function showError(kind) {
  clearJobs();state.error=kind;state.readyVersion=-1;state.result='';setLearn(false);connected(false);$('englishResult').className='english-result placeholder';
  const labels={offline:['暂时无法翻译，中文输入仍可用。','网络中断'],key:['API Key 无效，请在设置中检查。','连接失败'],unconfigured:['连接 DeepSeek 后显示英文。','未配置']};
  $('englishResult').textContent=labels[kind][0];setStatus(labels[kind][1],kind==='unconfigured'?'':'error');$('errorActions').hidden=false;
  $('retryTranslation').firstChild.textContent=kind==='offline'?'重新尝试 ':'打开设置 ';updateActions();
}
function selectState(mode) {
  resetExample(state.example);updateControls(mode);
  if(mode==='pinyin') {
    resetExample('tired');state.pendingPinyin=true;$('sentenceInput').value='我今天累坏了，想早点';$('pinyinRow').hidden=false;setStatus('待确认选词','busy');showNotice('空格或数字选择中文候选，先加入句子编辑区。');updateCount();updateActions();
  } else if(mode==='streaming') {
    const sequence=['我今天有点累。','我今天累坏了。','我今天累坏了，想早点休息。',examples.tired.source];
    const epoch=state.epoch;
    sequence.forEach((text,index)=>schedule(()=>{if(state.epoch!==epoch)return;$('sentenceInput').value=text;onSourceInput();showNotice('正在演示连续输入和周期更新；这里的时间是模拟值。');},index*620));
  } else if(mode==='explain-error') {state.explanationError=true;setLearn(true);}
  else if(['offline','key','unconfigured'].includes(mode))showError(mode);
  else if(state.scene==='learn')setLearn(true);
}
function onSourceInput() {
  let text=Array.from($('sentenceInput').value);if(text.length>300){$('sentenceInput').value=text.slice(0,300).join('');showNotice('当前草稿最多 300 字。');}else showNotice('');
  state.version++;state.pendingPinyin=false;$('pinyinRow').hidden=true;state.readyVersion=-1;setLearn(false);updateCount();updateActions();
  if(state.error)return;
  if(!$('sentenceInput').value.trim()){clearJobs();state.result='';$('englishResult').textContent='输入中文，看看英语怎么说。';$('englishResult').className='english-result placeholder';setStatus('等待输入');return;}
  state.pending={version:state.version,source:$('sentenceInput').value.trim()};setStatus('更新中','busy');queueTranslation();
}
function queueTranslation() {
  if(state.active||!state.pending)return;
  const wait=Math.max(0,500-(performance.now()-state.lastStart));
  if(wait>0){schedule(queueTranslation,wait);return;}
  const request=state.pending;state.pending=null;state.active=true;state.lastStart=performance.now();
  const entry=Object.values(examples).find(x=>x.source===request.source);
  const english=entry?.english||partials[request.source];
  if(!english){schedule(()=>{state.active=false;if(request.version===state.version){state.result='';$('englishResult').textContent='原型没有这句的示例翻译。请选择左侧内置例句查看效果。';$('englishResult').className='english-result placeholder';setStatus('离线演示');updateActions();}queueTranslation();},260);return;}
  const chunks=english.match(/.{1,10}/g)||[];let index=0;
  const render=()=>{
    $('englishResult').className='english-result';$('englishResult').textContent=chunks.slice(0,++index).join('');setStatus(request.version===state.version?'生成中':'更新中','busy');
    if(index<chunks.length)schedule(render,55);
    else{state.active=false;state.result=english;state.readyVersion=request.version;setStatus(request.version===state.version?'就绪':'更新中',request.version===state.version?'':'busy');updateActions();queueTranslation();}
  };schedule(render,180);
}
function chooseCandidate(word) {
  if(!state.pendingPinyin)return;
  $('sentenceInput').value='我今天累坏了，想早点'+word+'。';state.pendingPinyin=false;$('pinyinRow').hidden=true;onSourceInput();showNotice('候选已加入句子编辑区，尚未输入到目标软件。');
}
function commit(language) {
  if(language==='english'&&$('commitEnglish').disabled){showNotice(state.pendingPinyin?'请先确认当前中文候选。':'请等待当前中文对应的完整翻译。');return;}
  let source=$('sentenceInput').value;
  if(language==='chinese'&&state.pendingPinyin)source+='睡。';
  const value=language==='english'?state.result:source;if(!value.trim())return;
  $('targetText').textContent=value;clearJobs();state.version++;state.readyVersion=-1;state.result='';state.pendingPinyin=false;
  $('sentenceInput').value='';$('englishResult').textContent='';$('explanationContent').replaceChildren();setLearn(false);$('pinyinRow').hidden=true;$('imePanel').hidden=true;$('commitSuccess').hidden=false;updateCount();updateActions();
}
function applyTheme(choice) {
  state.theme=choice;const resolved=choice==='system'?(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light'):choice;document.documentElement.dataset.theme=resolved;
  document.querySelectorAll('[data-theme-choice]').forEach(b=>{b.classList.toggle('selected',b.dataset.themeChoice===choice);b.setAttribute('aria-pressed',String(b.dataset.themeChoice===choice));});
}
document.querySelectorAll('[data-scene]').forEach(b=>b.addEventListener('click',()=>b.dataset.scene==='settings'?startSettings():setScene(b.dataset.scene)));
document.querySelectorAll('[data-example]').forEach(b=>b.addEventListener('click',()=>{state.example=b.dataset.example;setScene(state.scene==='learn'?'learn':'input');}));
document.querySelectorAll('[data-candidate]').forEach(b=>b.addEventListener('click',()=>chooseCandidate(b.dataset.candidate)));
$('sentenceInput').addEventListener('input',onSourceInput);
$('sentenceInput').addEventListener('keydown',(event)=>{
  if(event.isComposing)return;
  if(event.key==='Enter'){event.preventDefault();commit(event.ctrlKey?'english':'chinese');}
  else if(state.pendingPinyin&&(event.key===' '||/^[1-4]$/.test(event.key))){event.preventDefault();chooseCandidate(['睡','水','税','谁'][event.key===' '?0:Number(event.key)-1]);}
});
document.addEventListener('keydown',(event)=>{
  if(event.isComposing||state.scene==='settings'||$('imePanel').hidden)return;
  const owned=$('imePanel').contains(document.activeElement);
  if(!owned)return;
  if(event.ctrlKey&&event.key.toLowerCase()==='j'){event.preventDefault();if(!$('learnToggle').disabled)setLearn($('explanationPanel').hidden);}
  if(event.key==='Escape'){
    event.preventDefault();if(state.pendingPinyin){state.pendingPinyin=false;$('pinyinRow').hidden=true;onSourceInput();}
    else if(!$('explanationPanel').hidden)setLearn(false);
    else{$('sentenceInput').value='';onSourceInput();}
  }
});
$('learnToggle').addEventListener('click',()=>setLearn($('explanationPanel').hidden));
$('commitChinese').addEventListener('click',()=>commit('chinese'));
$('commitEnglish').addEventListener('click',()=>commit('english'));
$('openSettings').addEventListener('click',startSettings);
$('closeSettings').addEventListener('click',()=>setScene('input'));
$('startAgain').addEventListener('click',()=>{resetExample(state.example);if(state.scene==='errors')setScene('input');else updateControls();});
$('retryTranslation').addEventListener('click',()=>{if(state.error==='offline'){state.error=null;connected();$('errorActions').hidden=true;onSourceInput();toast('网络恢复为演示状态，未调用真实服务。');}else startSettings();});
$('themeToggle').addEventListener('click',()=>applyTheme(document.documentElement.dataset.theme==='light'?'dark':'light'));
document.querySelectorAll('[data-theme-choice]').forEach(b=>b.addEventListener('click',()=>applyTheme(b.dataset.themeChoice)));
document.querySelectorAll('[data-density]').forEach(b=>b.addEventListener('click',()=>{state.density=b.dataset.density;$('imePanel').classList.toggle('compact',state.density==='compact');document.querySelectorAll('[data-density]').forEach(x=>{x.classList.toggle('selected',x===b);x.setAttribute('aria-pressed',String(x===b));});}));
$('testConnection').addEventListener('click',()=>{$('testConnection').disabled=true;$('testStatus').textContent='正在演示连接检查…';setTimeout(()=>{$('testStatus').textContent='连接成功（模拟，未发起网络请求）';$('testConnection').disabled=false;},450);});
$('saveSettings').addEventListener('click',()=>{$('settingsNotice').textContent='外观已应用到本次预览。关闭页面后不会保存。';toast('设置已应用到本次预览');});
matchMedia('(prefers-color-scheme: dark)').addEventListener('change',()=>{if(state.theme==='system')applyTheme('system');});
window.addEventListener('pagehide',clearJobs);
applyTheme('system');resetExample();updateControls();
