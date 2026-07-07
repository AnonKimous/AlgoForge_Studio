# AlgorithmScheduler Stage/Node 鍖栨敼閫犲啿绐佹鏌?

## 1. 缁撹鍏堣

鍩轰簬褰撳墠 checkout锛岃繖娆♀€渀AlgorithmScheduler` 涓嶅啀鎸佹湁鏁存潯 pipeline锛岃€屾槸鎸?stage / 鍗曠畻娉曞垎鍒寔鏈夛紝骞舵妸 stage 瑙嗕负 node 鏀捐繘鍝堝笇琛ㄢ€濈殑鏂瑰悜锛屽拰鐜版湁瀹炵幇瀛樺湪涓夌被纭啿绐侊細

- 褰撳墠鏂囨。鎶?`AlgorithmScheduler` 鏄庣‘瀹氫箟涓?pipeline runtime owner锛岃€屼笉鏄?node registry owner銆?
- 褰撳墠浠ｇ爜鎶?pipeline 褰撴垚涓€娈佃繛缁殑 stage 鏁扮粍鏉ユ寕杞姐€佹煡鎵俱€乼ick锛屼笉鏄澗鏁?node 鍥俱€?
- 褰撳墠 bridge / transfer 鍗忚鏄?`stage_name -> stage_container_set` 鐨?stage 鍚嶅瓧璺敱锛屼笉鏄?node id 璺敱銆?

濡傛灉鐩存帴鏀瑰瓨鍌ㄧ粨鏋勮€屼笉鍏堟敼 owner銆佺储寮曞拰鍗忚锛屾渶鍚庝細鍑虹幇涓€灞傝鈥滄寜 node 绠♀€濓紝鍙︿竴灞傝繕鍦ㄦ寜鈥滆繛缁?pipeline 鐗囨鈥濊窇鐨勬挄瑁傜姸鎬併€?

## 2. 褰撳墠鏋舵瀯浜嬪疄

### 2.1 鏂囨。灞傜洰鍓嶉粯璁?`AlgorithmScheduler` 鎸佹湁鏁存潯 pipeline

- [algorithm_scheduler_center.md](/D:/gptsandbox/Devlog/algorithm_scheduler_center.md:41) 绗?41-42 琛屽啓鏄庘€滄祦姘寸嚎绠楁硶鐨勮繍琛屾€佷笉鑳芥暎钀藉湪 `Agent` 閲岋紝璋冨害涓績瑕佷繚瀛樺搴旂殑杩愯鏃剁姸鎬佲€濄€?
- [algorithm_scheduler_center.md](/D:/gptsandbox/Devlog/algorithm_scheduler_center.md:77) 绗?77-84 琛屽啓鏄庤皟搴︿腑蹇冩寔鏈夆€滄祦姘寸嚎瀹氫箟銆乴ane 鍒楄〃銆乻tage 鐘舵€併€乷wner 褰掑睘銆佽繍琛岀粺璁♀€濄€?
- [algorithm_scheduler_pipeline_model.md](/D:/gptsandbox/Devlog/algorithm_scheduler_pipeline_model.md:12) 绗?12 琛屽啓鏄庘€滄祦姘寸嚎鐨勮皟搴︺€佹帹杩涖€佺粺璁°€佹寕杞藉拰鍗歌浇锛岄兘鐢?`AlgorithmScheduler` 缁熶竴绠＄悊鈥濄€?
- [algorithm_scheduler_pipeline_model.md](/D:/gptsandbox/Devlog/algorithm_scheduler_pipeline_model.md:124) 绗?124-132 琛岀户缁妸鈥滄寔鏈夋祦姘寸嚎 runtime銆佺淮鎶ゆ彁浜ゅ拰 tick 鍏崇郴銆佺淮鎶ゅ悓姝ョ粺璁♀€濆畾涔夋垚 `AlgorithmScheduler` 鑱岃矗銆?

### 2.2 鏈€鏂?`pipelineDevDoc` 宸叉敹鍙ｄ负鈥滅畻娉曡涔変笉涓?runtime_systems鈥?

- [devtask.md](/D:/gptsandbox/Devlog/devtask.md) 宸叉槑纭姹傦細JOBS pipeline 鐨勭畻娉曡涔夈€乷wner 鍜?runtime 鐘舵€佺暀鍦?`agent` / `algorithm_management`锛屽苟琛ラ綈 stage 闂翠复鏃惰嚜瀹氫箟鍙傛暟瑙勫垯銆?
- [devtask.md](/D:/gptsandbox/Devlog/devtask.md) 宸叉槑纭鏄?`runtime_systems` 涓嶅簲鐞嗚В algorithm / pipeline / stage / lane / runtime transfer map銆?
- [devtask.md](/D:/gptsandbox/Devlog/devtask.md) 宸叉妸鍒嗗眰閲嶆柊鏀跺彛涓猴細`agent` 璐熻矗鍏ュ彛锛宍algorithm_management::AlgorithmScheduler` 璐熻矗 pipeline 娉ㄥ唽涓庢帹杩涳紝`runtime_systems` 鍙繚鐣欎笅灞傛墽琛屽師璇€?
- [devtask.md](/D:/gptsandbox/Devlog/devtask.md) 宸蹭繚鐣欏苟缁熶竴浜?stage 闂翠复鏃惰嚜瀹氫箟鍙傛暟瑙勫垯锛屾妸瀹冧滑绾︽潫鍦?standard container / interStageBuffer / same-name custom container 杩欎簺閫氶亾閲屻€?

### 2.3 涓诲共灞傜骇绾︽潫涓嶅厑璁搁殢渚胯法灞傛敼 owner

- [AGENTS.md](/D:/gptsandbox/AGENTS.md:12) 绗?12 琛岃姹傝皟鐢ㄩ摼淇濇寔 `sdk -> agent_management -> agent -> algorithm_management -> runtime_systems`銆?
- [src/README.md](/D:/gptsandbox/src/README.md:9) 绗?9-12 琛岃姹備弗鏍间富骞插眰鍙兘渚濊禆涓嬩竴灞傚叕寮€鎺ュ彛銆?
- [src/README.md](/D:/gptsandbox/src/README.md:22) 绗?22-23銆?2銆?5-46 琛岃姹?`algorithm_management/algorithm_manager.h` 鍜?`runtime_systems/runtime_systems.h` 鍒嗗埆鏄悇灞傚敮涓€鍏紑鍏ュ彛銆?

## 3. 褰撳墠浠ｇ爜浜嬪疄

### 3.1 `AlgorithmScheduler` 鐜板湪灏辨槸鈥滄暣鏉?pipeline runtime 鎸佹湁鑰呪€?

- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:62) 鍒?[src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:108) 鏆撮湶鐨勬槸 `RegisterPipeline`銆乣RegisterPipelineRuntime`銆乣TryGetPipelineRuntime`銆乣UpdatePipelineRuntime` 杩欏鈥滄暣鏉?pipeline鈥濇帴鍙ｃ€?
- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:116) 鍒?[src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:120) 鐨勫唴閮ㄥ瓨鍌ㄤ篃鏄細
  `pipeline_registrations_`
  `pipeline_runtime_states_`
  `pipeline_runtime_ref_counts_`
- [src/algorithm_management/algorithm_abi.h](/D:/gptsandbox/src/algorithm_management/algorithm_abi.h:140) 鍒?[src/algorithm_management/algorithm_abi.h](/D:/gptsandbox/src/algorithm_management/algorithm_abi.h:192) 鐨?runtime 鏁版嵁缁撴瀯涓績涔熸槸 `JobsPipelineRegistration`銆乣JobsPipelineLaneRuntimeState`銆乣JobsPipelineRuntimeState`锛屾病鏈?node registry銆?

### 3.2 `Agent` 鐜板湪浠嶆妸 pipeline 褰撴垚鈥滆繛缁?stage 娈碘€?

- [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:363) 鍒?[src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:392) 鐨?`_FindPipelineGroupRange` 閫氳繃 `pipeline_name + 杩炵画 pipeline_stage_index` 鏉ヨ瘑鍒竴鏁存 pipeline銆?
- [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:1628) 鍒?[src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:1734) 鍦ㄦ寕杞芥椂锛屾妸姣忎釜 mounted `AlgorithmObject` 鍐欏叆 `pipeline_name`銆乣pipeline_stage_index`銆乣pipeline_stage_count`锛岀劧鍚庢暣鏉?pipeline 鐨?runtime 涓€娆℃€ф敞鍐岃繘 scheduler銆?
- [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:1886) 鍒?[src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:2145) 鐨?tick 璺緞锛屽厛鎸?begin/end stage 鍖洪棿鍙栧嚭涓€鏁寸粍瀵硅薄锛屽啀鎶婂畠浠綔涓轰竴涓?pipeline group 鎺ㄨ繘銆?

### 3.3 璋冨害鍣ㄥ唴閮?tick 浠嶆寜鈥滄暣缁?pipeline stage鈥濇墽琛?

- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1503) 鍒?[src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1540) 鐨?`TickMountedPipeline` 鍏ュ弬鏈韩灏辨槸 `mounted_objects + begin_index + end_index`銆?
- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1701) 鍒?[src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1724) 鍏堟瀯寤烘暣缁?`stage_container_sets`锛屽啀璁＄畻鍙墽琛?stage 闆嗗悎銆?
- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1878) 鍒?[src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:2135) 鐨勬墽琛屾祦绋嬩粛鏄€滄湰杞墍鏈夊彲鎵ц stage 鍋?ingress -> execute -> egress鈥濓紝鏈€鍚庝竴娆℃€ф彁浜ゆ暣缁?stage 鐘舵€併€?

### 3.4 鍗忚灞傝繕鏄?`stage_name -> container_set`锛屼笉鏄?`node_id -> runtime`

- [src/algorithm_catalog/algorithm_protocol.h](/D:/gptsandbox/src/algorithm_catalog/algorithm_protocol.h:81) 鍒?[src/algorithm_catalog/algorithm_protocol.h](/D:/gptsandbox/src/algorithm_catalog/algorithm_protocol.h:127) 鐨?ingress/egress/debug API 閮芥帴鍙?`target_stage_name` / `source_stage_name` 鍜?`unordered_map<string, shared_ptr<AlgorithmContainerSet>> stage_container_sets`銆?
- [src/algorithm_catalog/algorithm_package_decomposer.cpp](/D:/gptsandbox/src/algorithm_catalog/algorithm_package_decomposer.cpp:1270) 鍒?[src/algorithm_catalog/algorithm_package_decomposer.cpp](/D:/gptsandbox/src/algorithm_catalog/algorithm_package_decomposer.cpp:1276) 浠嶆寜 `target_stage_name` 鏌ュ叆杈癸紝骞舵柇瑷€鈥滀笉鍏佽澶氫釜鍓嶉┍鈥濄€?
- [src/algorithm_catalog/algorithm_package_decomposer.cpp](/D:/gptsandbox/src/algorithm_catalog/algorithm_package_decomposer.cpp:1326) 鍒?[src/algorithm_catalog/algorithm_package_decomposer.cpp](/D:/gptsandbox/src/algorithm_catalog/algorithm_package_decomposer.cpp:1356) 浠嶆寜 `source_stage_name` 鏌ュ嚭杈癸紝骞堕€氳繃 `stage_container_sets.find(outgoing_edge->target_stage_name)` 鎵句笅涓€ stage銆?

## 4. 涓庤繖娆?Stage/Node 鍖栨柟鍚戠殑鐩存帴鍐茬獊

### 4.1 Owner 鍐茬獊

鐜板湪鑷冲皯鏈変袱涓?owner 璇存硶锛?

- 鏂囨。 A锛歚AlgorithmScheduler` 鎸佹湁鏁存潯 pipeline銆?
- 鏂囨。 B锛歱ipeline 璇箟鐣欏湪 `agent` / `algorithm_management`锛宍runtime_systems` 涓嶇悊瑙?algorithm銆?
- 浣犵殑鏂版柟鍚戯細scheduler 涓嶅啀鎸佹湁鏁存潯 pipeline锛岃€屾槸鍒嗗埆鎸佹湁 stage/node銆?

杩欓噷鐪熸瑕佸畾鐨勬槸锛氬湪鈥滅畻娉曡涔変笉涓嬫矇鍒?`runtime_systems`鈥濊繖涓墠鎻愪笅锛宻tage/node registry 鏈€缁堣惤鍦?`agent` 鍏煎灞傦紝杩樻槸钀藉湪 `algorithm_management::AlgorithmScheduler`銆?

- 鏂规 A锛歚algorithm_management::AlgorithmScheduler` 鎸佹湁 node registry锛宍Agent` 淇濈暀鍏煎瑙嗗浘銆?
- 鏂规 B锛歚Agent` 缁х画鎸佹湁鏇村 pipeline/stage 瑙嗗浘锛宍AlgorithmScheduler` 鍙仛閮ㄥ垎娉ㄥ唽/璺敱銆?

浠庣幇鏈夊垎灞傚拰浠ｇ爜鐘舵€佺湅锛屾柟妗?A 鏇磋创杩?[devtask.md](/D:/gptsandbox/Devlog/devtask.md) 鍜?[src/README.md](/D:/gptsandbox/src/README.md:22)锛屼篃鏇村埄浜庢妸 pipeline 璇箟浠?`Agent` 缁х画鏀跺彛鍒?`algorithm_management`銆?

### 4.2 鏁版嵁妯″瀷鍐茬獊

褰撳墠 runtime 鐨勬牳蹇冪姸鎬佹槸锛?

- pipeline 绾э細`JobsPipelineRuntimeState`
- lane 绾э細`JobsPipelineLaneRuntimeState`
- stage 绾э細`stage_has_data`銆乣stage_runtime_stats`

濡傛灉鏀规垚 node registry锛岃嚦灏戣琛ヤ竴灞傛槑纭ā鍨嬶細

- `PipelineDefinition`
- `StageNodeDefinition`
- `StageNodeRuntimeState`
- `LaneCursor` 鎴?`LaneToken`

鍚﹀垯鍙槸鎶?`unordered_map` 鎹㈣繘鍘伙紝閫昏緫渚濈劧杩樻槸鈥滄暣鏉?pipeline 鐨勬暟缁勭姸鎬佲€濓紝閭ｄ笉鏄湡姝ｇ殑 node 鍖栥€?

### 4.3 `Agent` 瑙嗗浘鍐茬獊

`Agent` 褰撳墠榛樿鍋囪锛?

- pipeline 鍦?`algorithm_objects_` 閲屾槸涓€娈佃繛缁寖鍥达紱
- range 鏄€氳繃 `pipeline_stage_index` 椤哄簭璇嗗埆鐨勶紱
- tick 鏃跺彲浠ョ洿鎺ユ妸 begin/end 鑼冨洿浜ょ粰 scheduler銆?

杩欏拰鈥渘ode 绂绘暎鎸佹湁鈥濈洿鎺ュ啿绐併€傛渶灏忎篃寰椾簩閫変竴锛?

- 缁х画璁?`Agent` 淇濈暀杩炵画 mounted stage 瑙嗗浘锛屼絾 scheduler/runtime 鍐呴儴杞垚 node registry銆?
- 鎴栬€呭交搴曞彇娑?`Agent` 瀵?pipeline stage 鏁扮粍鐨勭洿鎺ョ悊瑙ｏ紝璁?`Agent` 鍙寔鏈?pipeline handle銆?

绗簩绉嶆洿骞插噣锛屼絾鏀瑰姩闈㈡槑鏄炬洿澶с€?

### 4.4 Bridge 鍗忚鍐茬獊

褰撳墠 bridge 鍗忚澶╃劧鍋?stage-name锛?

- 鍏ヨ竟/鍑鸿竟 lookup 鐢?stage 鍚嶏紱
- debug capture 鐢?stage 鍚嶏紱
- `stage_container_sets` 鐨?key 涔熸槸 stage 鍚嶃€?

濡傛灉 stage 琚彁鍗囦负 node锛屽繀椤绘槑纭細

- node key 鏄惁浠嶇劧鍏佽鐩存帴閫€鍖栨垚 `stage_name`锛?
- 鍚屼竴绠楁硶鍦ㄤ笉鍚?pipeline銆佷笉鍚?owner銆佷笉鍚?stage index 涓嬫槸鍚﹀厑璁搁噸鍚嶏紱
- debugTool 鏄樉绀?`stage_name` 杩樻槸 `node_id`銆?

鍙瀛樺湪鈥滃悓涓€涓畻娉曞悕鍦ㄥ涓?pipeline 鎴栧涓?owner 涓嬮噸澶嶆寕杞解€濓紝鍗曠嫭鐢?`stage_name` 鍋?key 灏变笉澶熶簡銆?

### 4.5 杩佺Щ璺緞鍐茬獊

褰撳墠浠撳簱閲屽瓨鍦ㄤ袱濂?JOBS pipeline 鎵ц閫昏緫锛?

- `Agent` 鍐呴儴杩樻湁涓€濂楁棫鐨?pipeline tick 涓诲惊鐜€?
- `AlgorithmScheduler::TickMountedPipeline` 鍙堟湁涓€濂楄皟搴﹀悗鐨勪富寰幆銆?

杩欒鏄庝粨搴撴澶勫湪鈥滀粠 agent 鍐呮敹鍙ｅ埌 scheduler/executor鈥濈殑杩囨浮鎬併€傛鏃跺啀鐩存帴涓?node 鍖栵紝濡傛灉涓嶅厛纭畾浠ュ摢涓€濂椾负鍑嗭紝寰堝鏄撴妸閲嶅閫昏緫鎵╁ぇ鎴愪笁濂椼€?

## 5. 鍝堝笇琛ㄤ笌鍝堝笇鍊煎缓璁?

### 5.1 宸叉湁宸ュ叿鍙互鐩存帴澶嶇敤

浠撳簱閲屽凡缁忔湁鐜版垚鐨?FNV-1a 64 浣嶅搱甯屽疄鐜帮紝鑰屼笖閲嶅浜嗕袱浠斤細

- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:346) 鍒?[src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:370)
- [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:30) 鍒?[src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:54)

寤鸿涓嶈鍐嶅啓绗笁浠斤紝鐩存帴鏀舵暃鎴愪竴涓叕鍏?helper锛屼緥濡傦細

- `algorithm_management/stage_node_hash.h`
- 鎴?`common_data/hash_utils.h`

### 5.2 Node Key 寤鸿

涓嶈鐩存帴鎷库€渟tage 鍚嶁€濆仛鍝堝笇 key锛屽缓璁渶灏戝寘鍚細

- `pipeline_name`
- `owner_agent_name`
- `stage_index`
- `stage_name`

濡傛灉鍚庨潰瑕佹敮鎸佸悓 pipeline 澶氬疄渚嬫垨鐑噸鎸傦紝鍐嶅姞锛?

- `pipeline_instance_id`
- 鎴?`registration_generation`

鎺ㄨ崘褰㈢姸锛?

```cpp
struct StageNodeKey {
  std::string pipeline_name;
  std::string owner_agent_name;
  uint32_t stage_index{0u};
  std::string stage_name;
};
```

鐒跺悗鎻愪緵锛?

- `bool operator==(const StageNodeKey&, const StageNodeKey&)`
- `struct StageNodeKeyHash`

濡傛灉杩橀渶瑕佹洿杞婚噺鐨勭储寮曪紝鍐嶆妸瀹冨帇鎴?`uint64_t stage_node_id`锛屼絾灞曠ず灞備粛淇濈暀鍙 key銆?

### 5.3 鍝堝笇琛ㄩ噷寤鸿鏀句粈涔?

濡傛灉鎸変綘杩欐鐩爣鎺ㄨ繘锛宯ode 琛ㄩ噷寤鸿鏀锯€滃崟 stage 鎸佹湁鐗┾€濓紝涓嶈缁х画鎶婃暣鏉?pipeline 濉炲洖鍘伙細

- stage 闈欐€佸畾涔?
- stage 瀵瑰簲绠楁硶瀵硅薄鎴栫畻娉曞彞鏌?
- stage runtime 鐘舵€?
- 鍏ヨ竟/鍑鸿竟寮曠敤
- debug 缁熻

lane銆乸ending stage0 submission銆乧ircular loopback 杩欎簺浠嶇劧鏇村儚 pipeline / lane 绾х姸鎬侊紝涓嶅缓璁‖濉炶繘鍗?node銆?

## 6. 杩欐鏀归€犲墠蹇呴』鍏堝畾鐨勬灦鏋勫喅绛?

寤鸿鍏堟槑纭笅闈?4 涓棶棰橈紝鍚﹀垯浠ｇ爜鏀瑰埌涓€鍗婁竴瀹氳繑宸ワ細

1. node registry 鏈€缁?owner 鏄?`Agent` 鍏煎灞傝繕鏄?`AlgorithmScheduler`锛?
2. `Agent` 鍚庣画鏄户缁寔鏈夆€渟tage 杩炵画鏁扮粍瑙嗗浘鈥濓紝杩樻槸鍙寔鏈?pipeline handle锛?
3. bridge / transfer map 鐨?lookup key 鏄户缁敤 `stage_name`锛岃繕鏄崌绾ф垚 `StageNodeKey` / `stage_node_id`锛?
4. lane 鐘舵€佹槸缁х画 pipeline-owned锛岃繕鏄媶鎴?node 鍙浣嗕笉褰?node 鎵€鏈夛紵

## 7. 鎺ㄨ崘鐨勬渶灏忚惤鍦伴『搴?

濡傛灉鎴戜滑瑕佸敖閲忓皯杩斿伐锛屽缓璁『搴忔槸锛?

1. 鍏堝畾 owner锛氬厛閫?`AlgorithmScheduler` 鎸佹湁 node registry锛岃繕鏄殏鏃剁户缁 `Agent` 淇濈暀鏇村鍏煎 ownership銆?
2. 鍏堟娊鍏叡鍝堝笇 helper锛屾妸鐜版湁 FNV-1a 涓や唤瀹炵幇鏀舵暃銆?
3. 鍏堝畾涔?`StageNodeKey`銆乣StageNodeDefinition`銆乣StageNodeRuntimeState`锛屽彧钀界被鍨嬶紝涓嶆敼璋冨害閫昏緫銆?
4. 鍐嶆妸 scheduler 鍐呴儴 `pipeline_runtime_states_` 鏃佽竟琛ヤ竴灞?node registry锛屽厛鍋氶暅鍍忕储寮曘€?
5. 绛?node registry 绋冲畾鍚庯紝鍐嶆敼 tick 璺緞锛屼粠鈥渞ange 椹卞姩鈥濋€愭鍒囧埌鈥渘ode 椹卞姩鈥濄€?
6. 鏈€鍚庢墠鏀?bridge 鍗忚鐨?key锛屼粠 `stage_name` 鍗囩骇鍒?node 绾?key銆?

## 8. 褰撳墠寤鸿

缁撳悎鐜版湁鏂囨。鍜屼唬鐮侊紝鎴戞洿寤鸿杩欐牱鐞嗚В杩欐鏀归€狅細

- 鐭湡锛氬厛鎶?`AlgorithmScheduler` 浠庘€滄暣鏉?pipeline runtime owner鈥濇敹鏁涙垚鈥減ipeline/node 娉ㄥ唽涓庤矾鐢变腑蹇冣€濄€?
- 涓湡锛氳 node registry 鎴愪负 scheduler 鍐呯殑绗竴灞傜储寮曪紝lane/runtime 浠嶆殏鏃朵繚鎸?pipeline-owned銆?
- 闀挎湡锛氱户缁寜 [devtask.md](/D:/gptsandbox/Devlog/devtask.md) 鐨勬柟鍚戯紝鎶?pipeline 璇箟鏀跺彛鍒?`algorithm_management`锛屽悓鏃惰 `runtime_systems` 缁存寔鈥滃彧鎻愪緵鎵ц鍘熻鈥濈殑杈圭晫銆?

杩欐牱鍜岀幇鏈夊垎灞傘€乧all chain銆佷互鍙?executor 鍖栨柟鍚戦兘鏇翠笉鍐茬獊銆?

## 9. 澶囨敞

杩欎唤妫€鏌ュ熀浜庡綋鍓嶆湰鍦颁粨搴撲唬鐮佷笌鏂囨。瀹屾垚銆侲cho Engine 鐭ヨ瘑搴撴煡璇㈠湪鏈幆澧冮噷琚嚜鍔ㄥ鎵规嫤涓嬶紝鍥犳杩欓噷鐨勭粨璁哄叏閮ㄤ互褰撳墠 checkout 涓哄噯锛屾病鏈夐澶栧紩鍏ョ煡璇嗗簱渚х殑鍙ｅ緞銆?
