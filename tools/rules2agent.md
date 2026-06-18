# rules2agent

## Standard Slot Alias Note

- `a1:vertex` and `v1,v2:pos` may appear in tool-side container semantics.
- These aliases are only for readability, grouping, and authoring.
- When a package field will be parsed by the current trunk runtime, the tool must emit the underlying `vN/aN` names instead of alias names.

请严格遵守下面的 UI 规则：

1. `containerElement` 的右键详情必须支持递归嵌套展示，使用括号树风格表达层级。
2. 详情里的成员显示必须只反映该容器当前包含的变量、数组和子容器。
3. 示例格式参考：
   - `volecity { v1, v2, v3 }`
   - `vertex { a1 }`
   - `pos { x, y }`
   - `pos { x { a2 }, y { a3 } }`
4. 容器/分组拖动时，内部成员和子容器必须一起移动，不能散开。
5. 容器内部画布缩小时，成员可以被裁剪遮住，但不能被强行拖出容器边界。
