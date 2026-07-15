# DevTask: Agent / Algorithm / RuntimeSystem 杈圭晫鏀跺彛

## 鏂囨。鐘舵€?

- 鏈枃妗ｇ敤浜庢浛浠ｆ棫 `pipelineDevDoc.txt` 涓庢棫 `devtask.md`銆?
- 濡傛灉鍘嗗彶鎻忚堪涓庢湰鏂囨。鍐茬獊锛屼互鏈枃妗ｄ负鍑嗐€?
- 鏈疆鍏堢‘璁ゆ枃妗ｅ彛寰勶紝纭鍚庡啀鎸夋湰鏂囨。鎺ㄨ繘浠ｇ爜淇敼銆?

## 鑳屾櫙

褰撳墠涓诲共浠诲姟鑱氱劍浜?pipeline / scheduler / agent / runtime_systems 鐨勫垎灞傛敹鍙ｃ€?

涓?Python 宸ュ叿渚х浉鍏崇殑绠楁硶浜х墿鐢熸垚銆佹墦鍖呫€佽杞借鍒欙紝涓嶅苟鍏ヨ繖浠戒富骞?`devtask`銆?

## 鎬荤洰鏍?

### 鐩爣锛氭敹绱ц繍琛屾椂杈圭晫

1. `runtime_systems` 涓嶅啀鎵挎帴浠讳綍 algorithm 涓撳睘姒傚康銆佺被鍨嬨€乷wner 璇箟鍜岃皟搴﹁涔夈€?
2. `Agent` 瀵圭畻娉曠殑浜嗚В鏀跺彛鍒版渶灏忚竟鐣岋細
   - 瀹冪煡閬撹嚜宸辨寔鏈?`AlgorithmObject`
   - 瀹冪煡閬撹繖鏄櫘閫氱畻娉曡繕鏄?pipeline 绠楁硶
   - 濡傛灉鏄?pipeline锛屽畠鍙煡閬撴嫇鎵戞槸 `linear` 鎴?`circular`
   - 闄ゆ涔嬪锛宍Agent` 涓嶄富鍔ㄦ媶瑙ｇ畻娉曞唴閮?stage / lane / bridge / intervention / reflector 缁嗚妭
3. `AlgorithmObject` 鎸佹湁 pipeline 鐩稿叧淇℃伅鏄厑璁哥殑锛涢棶棰樹笉鍦?`obj` 鎸佹湁锛岃€屽湪 `Agent` 涓嶅簲鑷繁鍔ㄦ墜鎷?`obj` 鍐呴儴缁撴瀯骞堕┍鍔ㄧ粏绮掑害鎵ц銆?
4. pipeline 鐨勫唴閮ㄦ帹杩涖€侀『搴忋€乴ane銆乻tage runtime 浠嶅睘浜?`algorithm_management` / `AlgorithmScheduler` 璇箟锛屼笉涓嬫矇鍒?`runtime_systems`銆?

## 鏈疆瀹炴柦鑼冨洿

### P0锛氬厛鍋氳竟鐣屾敹鍙?

1. 缁х画鎶?`runtime_systems` 鍐呬笌 algorithm 鐩存帴鑰﹀悎鐨勫唴瀹规嫈鎺夈€?
2. 妫€鏌ュ苟鍓婂噺 `Agent` 瀵?pipeline 鍐呴儴鎵ц缁嗚妭鐨勭洿鎺ヤ粙鍏ャ€?
3. 淇濇寔璋冪敤閾句粛涓猴細
   `sdk -> agent_management -> agent -> algorithm_management -> runtime_systems`
4. 涓嶅厑璁搁潤榛樺け璐ワ紱鐘舵€佷笉涓€鑷寸洿鎺ユ姤閿欐垨鏂█銆?

### P1锛氭暣鐞嗚皟璇曡兘鍔涗笌姝ｅ紡杩愯鑳藉姏鐨勮竟鐣?

1. `intervention`銆乣reflector`銆乣result render` 灞炰簬寮€鍙?璋冭瘯璇箟锛屼笉鏄甯歌繍琛屾€佽涔夈€?
2. 鍦ㄧ湡瀹炶繍琛岀畻娉曚腑锛岄粯璁や笉瑕佹眰瀛樺湪 `intervention` / `reflector`銆?
3. `Agent` 涓嶅簲璇ユ妸搴曞眰浠嬪叆鍣ㄥ綋鎴愯嚜宸辩殑淇″彿鎺у埗鍣紱绠楁硶淇″彿搴旂敱绠楁硶鑷韩璐熻矗銆?
4. `result render` 鍙兘鐣欏湪 `debugTool` 璇涓嬶紝涓嶈兘杩涘叆 release 杩愯鏃躲€?
5. `build release` 鏃讹細
   - 蹇呴』鐮嶆帀娓叉煋鍣?
   - `intervention` / `reflector` 瑕佷箞琚紑鍙戣€呮彁鍓嶆敹缂栬繘绠楁硶鏈綋锛岃涔堢洿鎺ヨ鍓旈櫎
   - 杩欒疆瀹炵幇鍙礋璐ｂ€滅爫鈥濓紝涓嶈礋璐ｈ嚜鍔ㄦ敹缂?
6. `build debug` 鏃讹細
   - 涓嶄涪浠讳綍璋冭瘯鏋勪欢
   - 闇€瑕佹槑纭鏄庯細甯﹀畬鏁磋皟璇曟瀯浠剁殑绠楁硶鍙厑璁稿湪璋冭瘯鍣ㄨ矾寰勪笅杩愯

### P2锛氱粺璁¤兘鍔涙敼鎴愭樉寮忓惎鐢?

1. `Agent` 瀵圭畻娉曡€楁椂鏁版嵁鍙繚鐣欎竴涓笂灞傛帴鍙ｃ€?
2. 璇ユ帴鍙ｇ殑鑱岃矗鏄€滆姹傝緭鍑轰竴浠芥棩蹇椻€濄€?
3. 鍙湁鍚敤璇ユ帴鍙ｆ椂锛屽簳灞傛墠寮€鍚粺璁￠€昏緫銆?
4. 涓嶅啀鎶婇€?stage / 鏅€氱畻娉曡€楁椂缁熻榛樿濉炶繘鏍稿績鐑矾寰勩€?

## 鏈疆鏄庣‘涓嶅仛鐨勪簨

1. 涓嶅湪杩欎竴杞妸鎵€鏈?stage 鍏ㄩ噺鎶借薄鎴?node銆?
2. 涓嶅湪杩欎竴杞妸 `AlgorithmScheduler` 鏀规垚鎸?node 鍝堝笇琛ㄥ畬鍏ㄦ寔鏈夈€?
3. 涓嶅湪杩欎竴杞敼鎺夌幇鏈?pipeline 鐨勨€滀袱娈垫彁浜も€濊涔夈€?
4. 涓嶅湪杩欎竴杞敼绠楁硶搴曞眰 `vn/an` 瀹瑰櫒瑙勫垯銆?
5. 涓嶅湪杩欎竴杞慨鏀?`CMakeLists.txt`銆?
6. 涓嶅湪杩欎竴杞妸 Python 宸ュ叿渚х殑浜х墿鐢熸垚銆佹墦鍖呫€佽杞介渶姹傚苟鍏ヤ富骞叉暣鏀硅寖鍥淬€?

## 褰撳墠宸茬‘璁ょ殑鏋舵瀯鍒ゆ柇

1. `runtime_systems` 涓嶅簲璇ョ悊瑙ｏ細
   - `AlgorithmObject`
   - pipeline
   - stage
   - lane
   - runtime transfer map
   - algorithm owner / 璋冨害璇箟
2. `AlgorithmScheduler` 褰撳墠缁х画浣滀负 pipeline 璇箟鐨勪富瑕佹壙鎺ュ眰锛屾槸鍙帴鍙楃殑杩囨浮鐘舵€併€?
3. `lane` 璇箟鏈韩鏄悎鐞嗙殑锛屼絾璋冨害椤哄簭涓嶅簲璇ョ敱 `Agent` 鑷繁澶勭悊銆?
4. `Agent` 搴旀妸鈥滃唴閮ㄩ偅涓€澶у潹鈥濇暣浣撹涓虹畻娉曪紝涓嶅簲鎶婂畠鎷嗘垚鈥滀粙鍏ュ櫒 + 鍙嶅皠鍣?+ 绠楁硶鏈綋 + 浠嬪叆娓叉煋鈥濈殑澶氫釜杩愯鏃舵帶鍒跺璞°€?
5. `AlgorithmObject` 鎸佹湁鎶婄绾挎帹閫佺粰璋冨害涓績鎵€闇€鐨勪俊鎭槸姝ｅ父鐨勶紝鏆傛椂涓嶄綔涓烘湰杞棶棰樻簮澶淬€?

## 涓存椂鑷畾涔夊弬鏁拌鍒?

### 1. 涓夌被鏁版嵁鍒嗗眰

1. stage 鍐呴儴 scratch
   - 鍙湪褰撳墠 stage 鎵ц杩囩▼涓复鏃跺瓨鍦?
   - 涓嶈法 stage
2. stage 闂翠复鏃跺弬鏁?
   - 褰撳墠 stage 浜у嚭锛屼笅涓€ stage 绔嬪嵆娑堣垂
   - 鍙敤浜庡皯閲忕鏁版嵁浼犻€?
   - 涓嶈涓?lane 闀挎湡涓荤姸鎬?
3. lane 闀挎湡鐘舵€?
   - 蹇呴』杩涘叆 `standard container`
   - 涓嶅厑璁镐吉瑁呮垚涓存椂鍙傛暟閫氶亾

### 2. 鍏佽鐨勮法 stage 浼犻€掓柟寮?

1. 棰濆 `v` 妲戒綅
   - 閫氳繃 `extra_variable_count` / `extra_variable_offset` 涓€绫绘槧灏勪俊鎭弿杩?
   - JOBS 璺緞璧板叡浜?`interStageBuffer`
   - VK 璺緞璧板叡浜?`stageBuffer`
2. 鍚屽悕鍚岀粨鏋勭殑 custom container
   - 浠呭厑璁?`custom -> custom`
   - 鍚嶇О蹇呴』鐩稿悓
   - 缁撴瀯蹇呴』瀹屽叏涓€鑷?

### 3. 鏄庣‘绂佹鐨勬儏鍐?

1. `standard slot <-> custom container` 娣蜂紶
2. custom container 璺?stage 鏀瑰悕
3. 鍏变韩鍓嶇紑涔嬪鏂板棰濆鏍囧噯 `a`
4. 鎶?`interStageBuffer` 褰撴垚瀹屾暣 stage 鐘舵€佸壇鏈?

### 4. 閫夋嫨瑙勫垯

1. 鍙湪褰撳墠 stage 鍐呴儴浣跨敤鐨勬暟鎹細鏀?scratch
2. 鍙渶浼犵粰涓嬩竴 stage 鐨勫皯閲忎复鏃舵爣閲忥細浼樺厛璧伴澶?`v` + offset
3. 蹇呴』浠ュ鍣ㄥ舰鐘惰法 stage 浼犻€掔殑鏁版嵁锛氬彧鑳借蛋鍚屽悕鍚岀粨鏋?custom container
4. 闇€瑕佽法澶氫釜 tick 鐨勬暟鎹細鍗囨牸涓?`standard container`

## 涓庣幇鐘剁殑涓昏鍐茬獊

### 澶у啿绐?

1. `Agent` 閲屼粛娈嬬暀澶ч噺 pipeline 鍐呴儴閫昏緫锛屽寘鎷?stage 椤哄簭銆乴ane 鎺ㄨ繘銆乥ridge debug銆乺eplay銆侀€?stage timing 绛夈€?
2. 杩欏拰鈥渀Agent` 鍙妸鍐呴儴瑙嗕负绠楁硶鏁翠綋鈥濈殑鐩爣瀛樺湪姝ｉ潰鍐茬獊銆?

### 灏忓啿绐?

1. `AlgorithmObject` 鍐呬粛甯︽湁杈冨 pipeline 鍏冩暟鎹€?
2. 杩欓儴鍒嗗綋鍓嶈涓哄彲鎺ュ彈鍘嗗彶鍖呰⒈锛屼笉浣滀负绗竴杞暣鏀归噸鐐广€?

### 寤舵湡椤?

1. stage/node 鍖栨敼閫犲啿鍑婚潰澶ぇ銆?
2. 濡傛灉鏈潵閲嶅惎杩欎欢浜嬶紝瑕佸湪 owner銆乴ookup key銆乴ane 妯″瀷銆乨ebug 瑙嗗浘缁熶竴鍚庡啀鍋氥€?
3. 杩欎竴杞厛涓嶆妸瀹冩贩鍏ュ疄鏂借寖鍥淬€?

## 楠屾敹鍙ｅ緞

### 杈圭晫鏀跺彛楠屾敹

1. `runtime_systems` 鐨勫叕寮€鎺ュ彛涓庡疄鐜颁腑锛屼笉鍐嶅嚭鐜?algorithm 涓撳睘 owner/璋冨害璇箟銆?
2. `Agent` 涓嶅啀缁х画鎵╁ぇ瀵?pipeline 鍐呴儴缁撴瀯鐨勭洿鎺ユ帶鍒躲€?
3. 鑰楁椂缁熻鏀规垚鏄惧紡鍚敤锛岃€屼笉鏄粯璁ゅ父椹汇€?
4. 璋冭瘯鏋勪欢鍜屾寮忚繍琛屾瀯浠舵湁鏄庣‘杈圭晫銆?

## 鎵ц椤哄簭寤鸿

1. 鍏堟寜鏈枃妗ｇ户缁仛 `runtime_systems` 鍘?algorithm 鑰﹀悎銆?
2. 鍐嶆敹 `Agent` 瀵?pipeline 鍐呴儴鎵ц缁嗚妭鐨勭洿鎺ヤ粙鍏ャ€?
3. 鍐嶆妸缁熻鑳藉姏鏀规垚鏄惧紡鍚敤銆?
4. 鍐嶆暣鐞?debug / release 涓?intervention / reflector / render 鐨勮竟鐣屻€?
