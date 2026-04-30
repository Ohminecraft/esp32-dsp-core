import os
import shutil
import hashlib
import glob

Import("env")

# Đường dẫn
PROJECT_DIR = env.get("PROJECT_DIR")
WEB_APP_DIR = os.path.join(PROJECT_DIR, "dsp-control-app")
DATA_DIR = os.path.join(PROJECT_DIR, "data")
HASH_FILE = os.path.join(PROJECT_DIR, ".last_web_ui_hash")

def get_dir_hash(directory):
    hasher = hashlib.md5()
    # Chỉ quét các file liên quan đến Web UI
    files = []
    for ext in ['*.html', '*.css', '*.js', '*.png', '*.jpg', '*.svg']:
        files.extend(glob.glob(os.path.join(directory, "**", ext), recursive=True))
    
    if not files:
        return "empty"
        
    for filename in sorted(files):
        with open(filename, 'rb') as f:
            while True:
                data = f.read(65536)
                if not data:
                    break
                hasher.update(data)
    return hasher.hexdigest()

def sync_web_ui(source, target, env):
    print("\n[WebUI-Sync] Starting synchronization...")
    
    # 1. Tạo thư mục data nếu chưa có
    if not os.path.exists(DATA_DIR):
        os.makedirs(DATA_DIR)
        
    # 2. Copy JS
    src_js = os.path.join(WEB_APP_DIR, "js")
    dst_js = os.path.join(DATA_DIR, "js")
    if os.path.exists(src_js):
        if os.path.exists(dst_js): shutil.rmtree(dst_js)
        shutil.copytree(src_js, dst_js)
        print("[WebUI-Sync] Copied /js")
    
    # 3. Copy CSS
    src_css = os.path.join(WEB_APP_DIR, "css")
    dst_css = os.path.join(DATA_DIR, "css")
    if os.path.exists(src_css):
        if os.path.exists(dst_css): shutil.rmtree(dst_css)
        shutil.copytree(src_css, dst_css)
        print("[WebUI-Sync] Copied /css")
    
    # 4. Copy index.html
    src_html = os.path.join(WEB_APP_DIR, "index.html")
    if os.path.exists(src_html):
        shutil.copy2(src_html, os.path.join(DATA_DIR, "index.html"))
        print("[WebUI-Sync] Copied index.html")
    
    # 5. Kiểm tra mã băm để quyết định có uploadfs hay không
    new_hash = get_dir_hash(DATA_DIR)
    old_hash = ""
    if os.path.exists(HASH_FILE):
        with open(HASH_FILE, 'r') as f:
            old_hash = f.read().strip()
            
    if new_hash != old_hash:
        print("[WebUI-Sync] Changes detected! Triggering SPIFFS upload...")
        with open(HASH_FILE, 'w') as f:
            f.write(new_hash)
        
        # Chạy lệnh uploadfs của PlatformIO
        env.Execute("pio run -t uploadfs")
    else:
        print("[WebUI-Sync] No changes in Web UI. Skipping SPIFFS upload.\n")

# Đăng ký chạy trước khi 'upload' firmware
env.AddPreAction("upload", sync_web_ui)
