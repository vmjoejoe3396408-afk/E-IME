# 第三方組件與資料授權聲明 (Third-Party Notices & Licenses)

本專案（E-IME）原始碼採用 **MIT License** 釋出。專案中包含或衍生自以下開源專案之資料與詞庫，各組件版權歸原作者所有，依其原開源授權條款分發。

---

## 1. libchewing-data (酷音輸入法詞庫資料)

* **專案網址**：https://github.com/chewing/libchewing-data
* **使用範圍**：本專案之核心詞庫檔案 [`data/core_dict.txt`](data/core_dict.txt) 係透過轉換腳本提取並整合自 `libchewing-data` 儲存庫中之 `dict/chewing/tsi.csv` 與 `word.csv`。
* **授權協議**：GNU Lesser General Public License version 2.1 or later (LGPL-2.1-or-later)
* **版權所屬**：Copyright (c) Chewing Developers (酷音輸入法開發團隊)

### GNU LESSER GENERAL PUBLIC LICENSE (LGPL v2.1) 條款摘要
> This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.
> 
> This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
> 
> Full text available at: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html

---

## 2. OpenCC (Open Chinese Convert 開放中文轉換)

* **專案網址**：https://github.com/BYVoid/OpenCC
* **使用範圍**：本專案之簡繁漢字轉換表 [`engine/include/hanzi_conv.h`](engine/include/hanzi_conv.h)（透過 [`tools/generate_hanzi_conv.py`](tools/generate_hanzi_conv.py) 產出）係參考並衍生自 OpenCC 之繁簡字元對照資料 (`data/dictionary/TSCharacters.txt`)。
* **授權協議**：Apache License, Version 2.0
* **版權所屬**：Copyright (c) 2010-2023 BYVoid and OpenCC Contributors

### Apache License Version 2.0 授權條款
```text
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
