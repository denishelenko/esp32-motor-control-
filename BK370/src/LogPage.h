#pragma once

namespace bk370 {
constexpr char LOG_HTML[] = R"HTML(
<!doctype html><html lang="uk"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Журнал BK370</title>
<style>
body{background:#14191d;color:#e8eef1;font:1rem system-ui;margin:0;padding:clamp(12px,3vw,28px)}
main{max-width:1180px;margin:auto}
header{display:flex;flex-wrap:wrap;align-items:baseline;justify-content:space-between;gap:12px}
h1,h2{margin:0}a{color:#a8d8fe}p{color:#aebec7}
.terminal{border:1px solid #3c5059;border-radius:10px;overflow:hidden;background:#080e11}
.terminalBar{padding:8px 14px;background:#202b31;color:#d3e1e7;font:.84rem Consolas,monospace}
pre{height:clamp(220px,67vh,720px);overflow:auto;margin:0;padding:14px;white-space:pre-wrap;overflow-wrap:anywhere;color:#9de5bc;font:.84rem/1.45 Consolas,monospace}
body.withPower pre{height:clamp(210px,42vh,500px)}
.power{margin-bottom:18px;border:1px solid #3c5059;border-radius:10px;padding:14px;background:#1c252b}
.power header{align-items:center}
select{padding:6px 9px;color:inherit;background:#26363f;border:1px solid #59717e;border-radius:6px;font:inherit}
.charts{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin-top:14px}
.chart{min-width:0;padding:10px;border:1px solid #3c5059;border-radius:8px}
.chart-current{grid-column:1/-1}
.chart h3{margin:0;font-size:.93rem}
.chart p{margin:.45rem 0;font-size:.84rem;font-variant-numeric:tabular-nums}
canvas{display:block;width:100%;height:clamp(145px,24vh,230px)}
#quality{color:#ffd490}
@media(max-width:660px){.charts{grid-template-columns:1fr}}
</style>
<main><header><h1>Журнал BK370</h1><a href="/">Керування</a></header>
<p id="state" role="status">Очікування даних ESP32…</p>
<section id="power" class="power" hidden>
 <header><h2>INA226</h2>
  <select id="carriage" aria-label="Каретка"><option value="0">BK1</option><option value="1">BK2</option></select>
 </header>
 <div class="charts">
  <section class="chart chart-current">
   <h3>Струм</h3><p id="values">Очікування вимірів…</p>
   <p id="quality" hidden>Датчик досяг межі вимірювання. Справжній пік може бути вищим.</p>
   <canvas id="currentPlot" role="img" aria-label="Графік струму"></canvas>
   <p>Середній струм — бірюзова лінія. Пік інтервалу — помаранчева.</p>
  </section>
  <section class="chart">
   <h3 id="voltageTitle">Напруга</h3><p id="voltageValue">—</p>
   <canvas id="voltagePlot" role="img" aria-label="Графік напруги"></canvas>
  </section>
  <section class="chart">
   <h3>Заряд батареї</h3><p id="chargeValue">—</p>
   <canvas id="chargePlot" role="img" aria-label="Графік оцінки заряду"></canvas>
  </section>
 </div>
</section>
<section class="terminal" aria-label="Консоль ESP32">
 <div class="terminalBar">Рух і серво · BK1 / BK2</div>
 <pre id="log" aria-label="Події кареток" tabindex="0"></pre>
</section></main>
<script>
const out=document.getElementById("log"),state=document.getElementById("state");
const byId=id=>document.getElementById(id);
const power=byId("power"),carriage=byId("carriage");
const history=[[],[]];
let cursor=0,session=0,powerCursor=0;
const isNumber=Number.isFinite;
const amps=value=>isNumber(value)?value.toFixed(2)+" А":"—";
function drawPower(){
 if(power.hidden)return;
 const points=history[Number(carriage.value)],last=points.at(-1);
 const batteryFeed=Boolean(last&&last[6]&16);
 byId("values").textContent=last?
   "Середній струм "+amps(last[2])+" · Пік інтервалу "+amps(last[3])+" · Пусковий пік (500 мс) "+amps(last[5]):
   "Очікування вимірів…";
 byId("quality").hidden=!last||!(last[6]&10);
 byId("voltageTitle").textContent=batteryFeed?"Напруга батареї":"Напруга на вході драйвера";
 byId("voltageValue").textContent=isNumber(last?.[4])?last[4].toFixed(2)+" В":"—";
 byId("chargeValue").textContent=isNumber(last?.[7])?"≈ "+last[7].toFixed(0)+"% за напругою":
   last&&!batteryFeed?"Недоступно без вимірювання напруги батареї":"—";
 drawPlot(byId("currentPlot"),points,[[2,"#6ee4cc"],[3,"#ffb260"]],"А");
 drawPlot(byId("voltagePlot"),points,[[4,"#83b8f4"]],"В");
 drawPlot(byId("chargePlot"),points,[[7,"#c4e79a"]],"%");
}
function drawPlot(canvas,points,series,unit){
 const width=canvas.clientWidth,height=canvas.clientHeight,dpr=devicePixelRatio||1;
 if(width<80||height<60)return;
 canvas.width=Math.round(width*dpr);canvas.height=Math.round(height*dpr);
 const ctx=canvas.getContext("2d");
 ctx.scale(dpr,dpr);
 const left=51,right=width-10,top=12,bottom=height-27;
 const end=points.at(-1)?.[0]??0,start=Math.max(points[0]?.[0]??0,end-120000);
 const visible=points.filter(p=>p[0]>=start);
 const samples=visible.flatMap(p=>series.map(([column])=>p[column])).filter(isNumber);
 if(!samples.length){ctx.fillStyle="#b5c5ce";ctx.font="13px system-ui";ctx.fillText("Даних для графіка ще немає",left,35);return;}
 const min=unit==="В"?Math.floor(Math.min(...samples)-0.1):Math.min(0,Math.floor(Math.min(...samples)));
 const max=unit==="%"?100:Math.max(min+1,Math.ceil(Math.max(...samples)+0.1));
 const x=at=>left+(at-start)/Math.max(1000,end-start)*(right-left);
 const y=value=>bottom-(value-min)/(max-min)*(bottom-top);
 ctx.font="11px system-ui";
 for(let i=0;i<=4;i++){
  const value=min+(max-min)*i/4,row=y(value);
  ctx.strokeStyle="#3e525f";ctx.beginPath();ctx.moveTo(left,row);ctx.lineTo(right,row);ctx.stroke();
  ctx.fillStyle="#bdcad0";ctx.fillText(value.toFixed(unit==="%"?0:1)+" "+unit,2,row+4);
 }
 ctx.fillText((Math.max(0,end-start)/1000).toFixed(1)+" с історії",left,height-5);
 for(const [column,color] of series){
  ctx.strokeStyle=ctx.fillStyle=color;ctx.lineWidth=1.5;ctx.beginPath();
  let prev=null;
  for(const p of visible){
   if(!isNumber(p[column])){prev=null;continue;}
   if(prev===null||p[0]-prev>500){
    ctx.moveTo(x(p[0]),y(p[column]));
    ctx.lineTo(x(p[0])+1,y(p[column]));
   }else ctx.lineTo(x(p[0]),y(p[column]));
   prev=p[0];
  }
  ctx.stroke();
 }
}
function updatePower(data){
 const sources=Number(data.inaSources)||0;
 power.hidden=!sources;
 document.body.classList.toggle("withPower",Boolean(sources));
 if(!sources)return;
 const current=Number(carriage.value);
 for(let id=0;id<2;id++)carriage.options[id].disabled=!(sources&(1<<id));
 if(!(sources&(1<<current)))carriage.value=sources&1?"0":"1";
 for(const row of data.power||[]){
  const points=history[row[1]];
  if(!points)continue;
  points.push(row);
  if(points.length>1250)points.splice(0,points.length-1250);
 }
 powerCursor=Number(data.powerCursor)||0;
 drawPower();
}
carriage.addEventListener("change",drawPower);
addEventListener("resize",drawPower);
async function poll(){
 const controller=new AbortController();
 const deadline=setTimeout(()=>controller.abort(),3000);
 try{
  const reply=await fetch("/logs/events?after="+cursor+"&boot="+session+"&powerAfter="+powerCursor,{cache:"no-store",signal:controller.signal});
  if(!reply.ok)throw new Error();
  const data=await reply.json();
  if(session&&session!==data.boot){
   out.textContent="";
   history[0].length=history[1].length=0;
   powerCursor=0;
  }
  session=data.boot;cursor=data.cursor;
  if(data.lines.length){
   const atBottom=out.scrollTop+out.clientHeight>=out.scrollHeight-20;
   out.textContent=(out.textContent+data.lines.join("\n")+"\n").slice(-32000);
   if(atBottom)out.scrollTop=out.scrollHeight;
  }
  updatePower(data);
  state.textContent="На зв’язку · BK1 головна · BK2 додаткова";
 }catch{state.textContent="Немає зв’язку з ESP32. Повторне підключення…";}
 finally{clearTimeout(deadline);setTimeout(poll,600);}
}
poll();
</script></html>
)HTML";
}
