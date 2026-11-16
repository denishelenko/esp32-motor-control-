from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

out = Path(__file__).with_name('CARRIAGE_BOARD_WIRING.png')
im = Image.new('RGB', (2800, 2200), '#f4f6f7')
d = ImageDraw.Draw(im)
reg = 'C:/Windows/Fonts/arial.ttf'
bold = 'C:/Windows/Fonts/arialbd.ttf'
def T(x,y,s,size=24,c='#183044',b=False):
    d.text((x,y),s,font=ImageFont.truetype(bold if b else reg,size),fill=c)
def box(x,y,w,h,c):
    d.rounded_rectangle((x,y,x+w,y+h),radius=14,fill=c,outline='#536570',width=3)
def pin(x,y,label,side='left'):
    d.ellipse((x-8,y-8,x+8,y+8),fill='#ced9d8',outline='#253544',width=2)
    f=ImageFont.truetype(bold,20); w=d.textbbox((0,0),label,font=f)[2]
    d.text((x+14 if side=='left' else x-14-w,y-12),label,font=f,fill='white')
def line(points,c,w=6):
    d.line(points,fill=c,width=w,joint='curve')
def dot(x,y,c):
    d.ellipse((x-7,y-7,x+7,y+7),fill=c,outline='white',width=2)
R='#cd333e'; K='#283948'; P='#9b4383'; B='#007fae'; C='#19a7bd'; G='#229b67'; O='#d88b20'; V='#8c56a7'
d.rectangle((0,0,2800,105),fill='#173a54')
T(45,22,'КАРЕТКА • СХЕМА З ФІЗИЧНИМ РОЗТАШУВАННЯМ ПЛАТ',43,'white',True)
T(50,125,'Звіряйте назви контактів на своїх модулях. Перетини проводів без кружка не з’єднані.',26)
# Keep the relative component arrangement of the supplied photograph.
box(230,270,530,1420,'#202c36')
box(325,340,340,270,'#adb9be'); T(380,430,'ESP32-S3',45); T(405,490,'N16R8',30)
T(360,635,'DEVKITC-1',25,'white',True)
left=['3V3','3V3','RST','4','5','6','7','15','16','17','18','8','3','46','9','10','11','12','13','14','5V','GND']
right=['GND','43','44','1','2','42','41','40','39','38','37','36','35','0','45','48','47','21','20','19','GND','GND']
LP={}; RP={}
for i,(a,b) in enumerate(zip(left,right)):
    y=710+i*41; pin(250,y,a);pin(740,y,b,'right');LP[a]=(250,y);RP[b]=(740,y)
box(320,1630,125,95,'#bdc6c9');box(560,1630,125,95,'#bdc6c9');T(340,1660,'UART',21);T(580,1660,'USB',21)
# 16 pin TB6612, matching the user's referenced pinout.
box(1050,280,550,670,'#cd3a40');T(1120,300,'TB6612FNG',37,'white',True)
box(1210,450,240,280,'#243340');T(1250,540,'TB6612',27,'white',True)
la=['VM','VCC','GND','A1','A2','B1','B2','GND'];rb=['PWMA','AIN2','AIN1','STBY','BIN1','BIN2','PWMB','GND']; D={}
for i,(a,b) in enumerate(zip(la,rb)):
    y=385+i*72;pin(1070,y,a);pin(1580,y,b,'right');D[a]=(1070,y);D[b]=(1580,y)
# Boost at right; terminals follow labels rather than presumed exact pad positions.
box(1950,280,510,670,'#1d6597');T(1990,300,'ПІДВИЩУВАЧ',30,'white',True);box(2090,440,220,220,'#314653');T(2140,520,'470',52,'white',True)
for y,s in [(380,'IN+'),(450,'IN−'),(780,'OUT+'),(850,'OUT−')]:pin(1970,y,s)
T(2010,915,'контакти звірити',21,'white')
# 5 V converter beneath motor driver.
box(1050,1050,550,260,'#13846b');T(1090,1075,'MINI 560 • 5 В',30,'white',True);box(1250,1145,140,110,'#526676');T(1290,1175,'100',30,'white',True)
for x,y,s,side in [(1070,1150,'IN+','left'),(1070,1240,'IN−','left'),(1580,1150,'OUT+','right'),(1580,1240,'OUT−','right')]:pin(x,y,s,side)
# TXS bottom: pin ordering as visible in user's photograph.
box(1050,1440,620,260,'#1d5b9d');T(1220,1470,'TXS0108E',32,'white',True);box(1270,1535,150,90,'#26333d')
for i,s in enumerate(['OE','A8','A7','A6','A5','A4','A3','A2','A1','VA']):
 x=1080+i*63;pin(x,1515,s)
for i,s in enumerate(['GND','B8','B7','B6','B5','B4','B3','B2','B1','VB']):
 x=1080+i*63;pin(x,1660,s)
# Motor control wires, routed outside the module bodies.
line([RP['2'],(800,874),(800,250),(1630,250),(1630,D['AIN1'][1]),D['AIN1']],B)
line([(1630,D['AIN1'][1]),(1645,D['BIN1'][1]),D['BIN1']],B);dot(1630,D['AIN1'][1],B)
line([RP['1'],(815,833),(815,235),(1650,235),(1650,D['AIN2'][1]),D['AIN2']],C)
line([(1650,D['AIN2'][1]),(1670,D['BIN2'][1]),D['BIN2']],C);dot(1650,D['AIN2'][1],C)
line([RP['42'],(830,915),(830,220),(1690,220),(1690,D['PWMA'][1]),D['PWMA']],V)
line([(1690,D['PWMA'][1]),(1720,D['PWMB'][1]),D['PWMB']],V);dot(1690,D['PWMA'][1],V)
# Peripheral signal and power nets; legends label the common rails for clarity.
line([LP['15'],(170,LP['15'][1]),(170,1790),(1584,1790),(1584,1515)],G)
line([(1584,1660),(1584,1835),(1870,1835)],G)
line([LP['16'],(145,LP['16'][1]),(145,1875),(1870,1875)],R)
line([LP['17'],(120,LP['17'][1]),(120,1920),(1870,1920)],G)
T(1875,1815,'B1 → жовтий серво',23);T(1875,1855,'16 → резистор → LED R',23);T(1875,1900,'17 → резистор → LED G',23)
# Power architecture shown as a separate clear circuit to avoid false crossover connections.
T(75,1990,'ЖИВЛЕННЯ: 2S (+) → ВИМИКАЧ → IN+ обох перетворювачів; 2S (−) → IN− обох.',25,b=True)
T(75,2030,'5 В → ESP 5V, TXS VB, червоний серво. 12 В → VM TB6612FNG. Всі GND спільні.',25)
T(75,2070,'3V3 ESP → VCC + STBY TB6612FNG і VA + OE TXS. Окремий резистор для кожного кольору LED.',25)
T(75,2110,'Резерв: 8/9 I²C для INA226; 10/11/12 для можливого I²S. Контакти перетворювачів звірити.',23)
T(75,2150,'Перед підключенням плат виставити мультиметром 5,0 В і 12,0 В. Пункти живлення продубльовані текстом.',23)
# Explicit power rail diagram and endpoint components in their physical positions.
# Dashed-style separation between this panel and the motor control wiring keeps
# crossings readable. These rail labels correspond directly to module pads.
box(1910,1030,620,260,'#334a5f');T(1970,1060,'АКУМУЛЯТОР 2S',32,'white',True)
pin(1940,1150,'+');pin(1940,1230,'−');T(1995,1190,'7,4 В • максимум 8,4 В',22,'white')
box(1910,1400,620,230,'#293943');T(1970,1425,'СЕРВОПРИВІД',32,'white',True)
pin(1940,1510,'+5V');pin(1940,1560,'GND');pin(1940,1610,'SIGNAL')
# Motor drawings and leads to independent bridge outputs.
box(20,290,160,140,'#c4cdd0');T(45,335,'M1',35,b=True)
box(2630,290,150,140,'#c4cdd0');T(2660,335,'M2',35,b=True)
line([(180,335),(185,335),(185,195),(1020,195),(1020,D['A1'][1]),D['A1']],O,5)
line([(180,385),(200,385),(200,180),(1000,180),(1000,D['A2'][1]),D['A2']],O,5)
line([(2630,335),(2590,335),(2590,170),(990,170),(990,D['B1'][1]),D['B1']],B,5)
line([(2630,385),(2570,385),(2570,155),(970,155),(970,D['B2'][1]),D['B2']],B,5)
# Battery positive: through switch to both converter input positive pins.
box(1800,990,100,55,'#f1d5a6');T(1810,1002,'ON/OFF',14)
line([(1940,1150),(1835,1150),(1835,1045)],R,6)
line([(1835,990),(1835,970),(1790,970),(1790,380),(1970,380)],R,6)
line([(1790,970),(1775,970),(1775,1025),(1020,1025),(1020,1150),(1070,1150)],R,6)
dot(1790,970,R)
# Common ground return; all ground endpoints tie to the same bus.
line([(1940,1230),(1845,1230),(1845,1370),(1790,1370),(1790,1740),(900,1740),(900,1240),(1070,1240)],K,6)
line([(1790,1370),(1830,1370),(1830,450),(1970,450)],K,6)
line([(1790,1740),(1790,1760),(890,1760),(890,LP['GND'][1]),LP['GND']],K,6)
line([(1790,1760),(1780,1760),(1780,850),(1970,850)],K,6)
line([(1790,1760),(1760,1760),(1760,1240),(1580,1240)],K,6)
line([(1790,1760),(1740,1760),(1740,D['GND'][1]),D['GND']],K,6)
line([(1790,1760),(1080,1760),(1080,1660)],K,6)
line([(1790,1760),(1810,1760),(1810,1560),(1940,1560)],K,6)
# The two logic enable inputs receive 3.3 V, PWM inputs remain GPIO42.
line([LP['3V3'],(105,LP['3V3'][1]),(105,1380),(905,1380),(905,D['VCC'][1]),D['VCC']],V,5)
line([(905,D['VCC'][1]),(915,260),(1730,260),(1730,D['STBY'][1]),D['STBY']],V,5)
line([(905,1380),(1080,1380),(1080,1515)],V,5)
line([(1080,1380),(1647,1380),(1647,1515)],V,5)
# 5 V buck output branches to ESP, level shifter and servo supply.
line([(1580,1150),(1725,1150),(1725,1340),(865,1340),(865,LP['5V'][1]),LP['5V']],O,6)
line([(1725,1340),(1725,1690),(1647,1690),(1647,1660)],O,6)
line([(1725,1340),(1880,1340),(1880,1510),(1940,1510)],O,6)
# Motor 12 V supply and servo signal.
line([(1970,780),(1890,780),(1890,145),(950,145),(950,D['VM'][1]),D['VM']],P,6)
line([(1584,1660),(1584,1835),(1870,1835),(1870,1610),(1940,1610)],G,5)
T(1945,1265,'+ через вимикач',21,'white');T(1945,1295,'− спільний GND',21,'white')
T(1945,1650,'Живлення від 5 В',21,'white')
im.save(out)
print(out)
