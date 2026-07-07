# Algorithm Library Naming Notes

Use `v` for variables and `a` for arrays.

## Standard Name Format

`v{variable_count}a{array_count}_{purpose}`

- `vN` means the standard layout exposes `N` variable containers named `v1` through `vN`.
- `aN` means the standard layout exposes `N` array containers named `a1` through `aN`.
- The suffix after the standard name describes what the algorithm bundle does.

## Standard Container Alias

- 鏍囧噯瀹瑰櫒鐨勫簳灞傛灦鏋勪繚鎸佷笉鍙橈紝杩愯鏃跺彧璁?`vN` 鍜?`aN`銆?
- 寮€鍙戣€呭湪澹版槑瀹?`vXaY` 涔嬪悗锛屽彲浠ュ湪 `container` 閲岀户缁０鏄庤繖浜涙爣鍑嗘Ы浣嶇殑楂樺眰鍒悕锛屼緵浜哄拰 agent 璇诲彇銆?
- 鎺ㄨ崘鍐欐硶鏄細
  - `"aliases": ["a1:vertex", "v1,v2:pos"]`
- `a1:vertex` 琛ㄧず `a1` 鐨勫彲璇诲埆鍚嶆槸 `vertex`銆?
- `v1,v2:pos` 琛ㄧず `v1` 鍜?`v2` 杩欎竴缁勬爣鍑嗗彉閲忓叡鍚岀粍鎴愪竴涓珮绾у彉閲?`pos`锛屾寜鐗规畩鍙橀噺瑙勫垯鐞嗚В銆?
- 濡傛灉涓€涓埆鍚嶅彧鏄犲皠鍒板崟涓?`v` 鎴栧崟涓?`a`锛屽畠浠嶇劧鍒嗗埆閬靛惊鍙橀噺/鏁扮粍鑷繁鐨勮鍒欍€?
- 杩欎簺鍒悕鏄畻娉曚晶鐨勫０鏄庝俊鎭紝涓嶆敼鍙樺簳灞傚鍣ㄥ竷灞€銆?
- agent 鎴栧紑鍙戝伐鍏峰彲浠ョ洿鎺ヤ娇鐢ㄨ繖浜涢珮绾у悕瀛楄繘琛岃璁°€佹矡閫氬拰鐢熸垚銆?
- 涓€鏃﹁鍐欏叆褰撳墠涓诲共鍙墽琛岀殑 package 鍐呭锛屽繀椤绘妸杩欎簺鍒悕鍏ㄩ儴杩樺師鍥?`vN/aN`锛屼笉瑕佹妸鍒悕鐩存帴鍐欒繘渚濊禆杩愯鏃惰В鏋愮殑瀛楁閲屻€?

## Example

- `temporary_test_line_motion`
  - `v1`, `v2`, `v3`: initial point position
  - `a1`: moving point buffer

## Bundle Rules

- Keep the folder name, manifest name, and catalog entry name identical.
- Keep the prefix counts exact.
- Put the behavior description after the `vNxM` standard name.
- Prefer short, explicit bundle names that make the purpose obvious.

## devTask

- 鐘舵€侊細in_progress
- 鏈€鍚庢洿鏂帮細2026-06-22
- 褰撳墠杩涘害锛?
  - 宸插畬鎴愶細娴佹按绾挎寜鈥滀竴涓?pipeline 浣滀负涓€涓畻娉曞崟鍏冣€濇寕杞戒笌璋冨害锛岀洰鏍囪〃杩扮粺涓€涓衡€滃鍔犲悶鍚愨€濓紝涓嶅啀浣跨敤鈥滃姞閫熷崟娆℃墽琛屸€濄€?
  - 宸插畬鎴愶細runtime transfer map 宸叉敹绱т负鍗曞悜绾挎€?`stage -> nextStage`锛岀姝?fan-out / fan-in銆?
  - 宸插畬鎴愶細鍘?`runtimeDecomposer/runtimeReflector` 鐨勮亴璐ｅ凡缁忕粺涓€鏀跺彛鍒?`PipelineStageBridge`銆?
  - 宸插畬鎴愶細VK 渚х户缁妸闅愬紡 stageBuffer 缁戝畾鍦?standard container 鍏变韩 `a` 鍓嶇紑閲岋紱JOBS 渚ф敼涓哄悓涓€ lane 鐨?bridge 鍏变韩涓€浠?`interStageBuffer`锛屼笉鍐嶄緷璧栨爣鍑嗗鍣ㄩ噷鐨勯殣寮?stageBuffer 浣滀负鐪熸杩愯鏃剁紦鍐层€?
  - 宸插畬鎴愶細pipeline stall 浼氳緭鍑烘瘡涓?stage 鐨勮€楁椂鍜屽師鍥狅紝骞跺湪鍚庣瀵煎嚭 `csv` / `mermaid` 鏃跺簭鏂囦欢鍒?`artifacts/pipeline_timing/`銆?
  - 宸插畬鎴愶細stage 鏀寔澹版槑 `runtime.pipeline.externalWriteResetContainers`锛岃〃绀鸿繖浜涘鍣ㄥ繀椤绘瘡甯т粠澶栭儴閲嶅啓锛宻tage 鎵ц瀹屾垚鍚庣珛鍗虫竻绌恒€?
  - 宸茶烦杩囷細鏈€鍒濈殑鈥滅幆褰㈡帰閽堚€濇柟妗堝凡搴熷純锛屽悗缁互 stall 鏃ュ織鍜?fail-fast 涓轰富銆?

- [x] 0. 娴佹按绾挎湰璐ㄦ槸涓€缁勭畻娉曡褰撴垚涓€涓畻娉曟彁浜わ紝鐢?agent 鏉ユ⒊鐞嗗畠浠殑娴佹按鍏崇郴锛岀洰鏍囨槸澧炲姞鍚炲悙锛屼笉鏄姞閫熷崟娆℃墽琛屻€?
- [x] 1. 娴佹按绾块渶瑕佹槧灏勮〃锛屾爣鍑嗗鍣ㄦ敼鍐欐垚鏄犲皠琛紝骞堕殢绠楁硶鏈韩鎻愪氦鍒?runtime sys 杩欎竴灞傘€?
- [x] 2. 鏄犲皠琛ㄥ彧淇濈暀绾挎€?`stage->nextStage` 鍏崇郴锛屼笉鍐嶆敮鎸佷竴涓?stage 鏄犲皠鍒板涓?stage锛屼篃涓嶅啀鏀寔涓€涓?stage 鎺ユ敹澶氫釜 stage 鐨勬槧灏勩€?
- [skip] 3. 鍘熷鈥滅幆褰㈡帰閽堚€濇柟妗堝凡搴熷純锛涙寜鍚庣画鍐崇瓥涓嶅啀瀹炵幇銆?
- [x] 4. 鍘?`runtimeDecomposer/runtimeReflector` 鏂规宸茬敱缁熶竴鐨?`PipelineStageBridge` 鍙栦唬銆?
- [x] 5. debugTool 鍚庣浼氫繚鐣?pipeline 鎬昏€楁椂鍜屽悇 stage 鑰楁椂锛屽苟鍦?stall 鏃跺鍑?`csv` / `mermaid` 鍥炬枃浠躲€?
- [x] 6. 宸插鍔?`runtime.pipeline.externalWriteResetContainers`锛岀敤浜庡０鏄庢瘡甯у閮ㄩ噸鍐欍€乻tage 鎵ц鍚庣珛鍗虫竻绌虹殑瀹瑰櫒銆?
- [x] 7. 濡傛灉娴佹按绾跨畻娉曟寔缁?tick 涓嶅姩锛岀洿鎺ヨ緭鍑烘瘡涓?stage 鐨勮€楁椂鍜屽師鍥犳棩蹇楋紝鐒跺悗鏂█鎶ラ敊锛涚湡姝ｂ€滃脊鍑虹畻娉曗€濈殑鍔ㄤ綔浠嶇暀寰呭悗缁€?

- If a run stalls, inspect the latest logs under `testData/` first, especially `progress_probe.log`, `last_run.log`, and the runner server/client logs.

## Pipeline Mapping Notes

- pipeline 鐨?runtime transfer map 鐜板湪涓嶅彧鎻忚堪 `stage -> nextStage` 鐨勮竟銆?
- 瀹冭繕瑕佹弿杩版瘡涓?stage 鐨勬爣鍑嗗鍣ㄥ竷灞€鎽樿锛?
  - `declared_variable_count`
  - `declared_array_count`
  - `shared_variable_count`
  - `shared_array_count`
  - `extra_variable_count`
  - `extra_array_count`
  - `extra_variable_offset`
  - `extra_array_offset`
- 鍚箟鏄細
  - 鎵€鏈?stage 鍏变韩涓€娈电粨鏋勫吋瀹圭殑 `v/a` 鍓嶇紑
  - 鏌愪釜 stage 濡傛灉棰濆澶氬０鏄庝簡鍑犱釜 `v` 鎴?`a`锛岃繖浜涢澶栨Ы涓嶄細娣疯繘鍏变韩鍓嶇紑
  - 瀹冧滑浼氭寜鐓?stage 椤哄簭绱鍋忕Щ锛屽啀鐧昏鍒版槧灏勮〃閲?
- 褰撳墠涓诲共鍏堟敹绱ф垚鍙厑璁搁澶?`v`銆?
- 濡傛灉鏌愪釜 stage 棰濆澶氬嚭浜嗗叡浜墠缂€涔嬪鐨?`a`锛岃繍琛屾椂鐩存帴鏂█鎶ラ敊銆?
- 渚嬪瓙锛?
  - stage0 棰濆澶?1 涓?`v`锛屽亸绉绘槸 `0`
  - stage1 棰濆澶?2 涓?`v`锛屽亸绉绘槸 `1`
  - stage3 棰濆澶?1 涓?`v`锛屽亸绉绘槸 `3`
- VK pipeline 寮哄埗瑕佹眰闅愬紡 stageBuffer 钀藉湪鎵€鏈?stage 鍏变韩鐨?`a` 鍓嶇紑閲岋紝涓嶈兘鎸傚埌鏌愪釜 stage 绉佹湁澶氬嚭鏉ョ殑 `a` 涓娿€?
- JOBS pipeline 鐨?bridge 杩愯鏃朵笉鍐嶆妸杩欏潡闅愬紡 stageBuffer 褰撴垚鐪熸鐨勮法 stage 缂撳啿锛汣PU 浼氫负姣忔潯 lane 缁存姢涓€浠藉叡浜?`interStageBuffer`锛屾墍鏈?stage bridge 鍏卞悓璇诲啓杩欎唤缂撳啿锛屽苟鎸夋槧灏勮〃鍋忕Щ瑙ｉ噴棰濆 `v`銆?
- 濡傛灉鏌愪釜 stage 鐨勬煇浜涘鍣ㄥ繀椤绘瘡甯ч兘浠庡閮ㄩ噸鍐欙紝鍙互鍦?package 鐨?`runtime.pipeline.externalWriteResetContainers` 閲屽垪鍑哄畠浠紱璇?stage 鎵ц瀹屾垚鍚庯紝杩欎簺瀹瑰櫒浼氳绔嬪嵆娓呴浂锛岄伩鍏嶆棫鍊煎湪娴佹按绾块噷婊炵暀銆?

## Package Format

Use one unified package file per algorithm bundle:

- `<algorithm_name>_package.algoPrj`
- The file contains `container`, `decomposer`, `reflector`, and `intervention` sections.
- The same package file can be used by the host, SDK, and debug tool.
- VK tick shaders receive viewport width/height push constants and interpret algorithm-space positions as lower-left origin pixel coordinates before converting to clip space.
- `container` only describes storage layout such as count, shape, and scalar bit width. It does not declare whether payload bytes are `int`, `float`, or packed bits.
- Descriptor interpretation belongs to the algorithm side. Do not add semantic type hints in new package descriptions.
- Reflection decoding belongs to `reflector`.

Use `cjson` style comments in the package file examples:

- Any text after `//` is commentary for the agent and developer.
- `//` comments are not part of the runtime data model.
- Runtime parsers should ignore `//` comments before parsing the JSON body.

### Example

```cjson
{
  "algorithm_name": "temporary_test_line_motion", // bundle name
  "globalCfg": {
    "solvePrecision": "32", // global scalar bit width for the package
    "defaultPrecision": "32"
  },
  "container": {
    "variable": 3, // create v1..v3
    "variableArray": 1, // create a1
    "aliases": [
      "v1,v2,v3:initial_point",
      "a1:point_buffer"
    ]
  },
  "decomposer": {
    "res": {
      "buffer": {}
    },
    "description": [
      {
        "name": "point_position",
        "from": ["start_x", "start_y", "start_z"],
        "to": ["v1", "v2", "v3"]
      }
    ]
  },
  "reflector": {
    "name": "positionABS",
    "functionName": "direct",
    "items": [
      {
        "name": "positionABS",
        "from": ["a1"],
        "to": ["positionABS"],
        "reflectFun": "direct"
      }
    ]
  },
  "intervention": {
    "stages": {
      "resultRender": {
        "stage_name": "resultRender",
        "stage_kind": "resultRender",
        "used_algorithm_containers": {
          "arrays": [
            {
              "name": "a1",
              "kind": "array",
              "tuple_width": 3,
              "required": true
            }
          ],
          "variables": []
        },
        "shader": {
          "pipeline": "graphics",
          "vertex": "temporary_test_line_motion_vk_tick.vert",
          "fragment": "temporary_test_line_motion_vk_tick.frag"
        }
      },
      "resultRender": {
        "stage_name": "resultRender",
        "stage_kind": "resultRender",
        "functions": [
          "ApplyResultRender"
        ],
        "shader": {
          "pipeline": "graphics",
          "vertex": "temporary_test_line_motion_result_render.vert",
          "fragment": "temporary_test_line_motion_result_render.frag"
        }
      }
    }
  }
}
```

## Coordinate Convention

- Use the repo-wide left-handed convention.
- Treat `[0,0,0]` as the lower-left near corner.
- `+X` points right, `+Y` points up, and `+Z` points into the screen.
- Render preview coordinates use the lower-left corner of the preview content region as `[0,0]`.
- `resultRender` is the default place to attach VK-side render work before the actual preview render pass.
