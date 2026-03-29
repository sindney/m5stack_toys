"""
wb_scanner.py — WorkBuddy 本地数据扫描器

扫描 WorkBuddy 的本地 JSON 文件，提取工作区列表和任务状态。
数据路径:
  - storage.json                → 工作区列表  (backupWorkspaces.folders[].folderUri)
  - genie-history/{b64}/current.json → 工作区的当前对话 ID
  - todos/{conversationId}.json → 任务列表
"""

import json
import os
import base64
import logging
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional
from urllib.parse import unquote, urlparse

log = logging.getLogger("wb_scanner")

# ── WorkBuddy data paths ───────────────────────────────────────────────────
APPDATA = os.environ.get("APPDATA", "")
WB_GLOBAL = os.path.join(APPDATA, "WorkBuddy", "User", "globalStorage")
WB_COPILOT = os.path.join(WB_GLOBAL, "tencent-cloud.coding-copilot")
STORAGE_JSON = os.path.join(WB_GLOBAL, "storage.json")
TODOS_DIR = os.path.join(WB_COPILOT, "todos")
HISTORY_DIR = os.path.join(WB_COPILOT, "genie-history")


@dataclass
class TodoItem:
    id: str
    status: str          # "pending" | "in_progress" | "completed"
    content: str


@dataclass
class Conversation:
    conversation_id: str
    todos: list[TodoItem] = field(default_factory=list)
    updated_at: int = 0   # Unix ms timestamp


@dataclass
class Workspace:
    name: str              # Short name (folder basename)
    path: str              # Full path
    folder_uri: str        # file:// URI
    conversations: list[Conversation] = field(default_factory=list)


@dataclass
class ScanResult:
    workspaces: list[Workspace] = field(default_factory=list)
    scan_time: float = 0.0


def _uri_to_path(uri: str) -> str:
    """Convert file:// URI to local path. e.g. file:///d%3A/SDK/m5stack_toys → d:/SDK/m5stack_toys"""
    parsed = urlparse(uri)
    path = unquote(parsed.path)
    # Remove leading / on Windows (e.g. /d:/... → d:/...)
    if len(path) > 2 and path[0] == '/' and path[2] == ':':
        path = path[1:]
    return path


def _path_to_b64_dir(path: str) -> str:
    """
    Encode workspace path to base64 URL-safe directory name.
    WorkBuddy uses: base64url(path) with '=' replaced by '_' or stripped,
    '/' replaced by '_', '+' replaced by '-'.
    """
    # Normalize: WorkBuddy uses forward slash lowercase drive
    normalized = path.replace('\\', '/')
    b64 = base64.urlsafe_b64encode(normalized.encode('utf-8')).decode('ascii')
    # WorkBuddy replaces '=' with '_' at the end
    b64 = b64.rstrip('=')
    # Append __ suffix (double underscore for padding replacement)
    # Actually: base64url uses _ for padding. Let me check actual directories.
    # From data: "ZDovU0RLL201c3RhY2tfdG95cw__" for "d:/SDK/m5stack_toys"
    # Standard base64url of "d:/SDK/m5stack_toys" = "ZDovU0RLL201c3RhY2tfdG95cw=="
    # So '=' → '_'
    return b64.replace('=', '_') if '=' in b64 else b64 + '__' if len(b64) % 4 == 2 else b64 + '_' if len(b64) % 4 == 3 else b64


def _read_json(path: str) -> Optional[dict]:
    """Safely read a JSON file."""
    try:
        with open(path, 'r', encoding='utf-8') as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError, OSError) as e:
        log.debug(f"Cannot read {path}: {e}")
        return None


class WorkBuddyScanner:
    """Scans WorkBuddy local data files."""

    def __init__(self):
        self._last_result: Optional[ScanResult] = None

    def scan(self) -> ScanResult:
        """Full scan: workspaces → conversations → todos."""
        t0 = time.time()
        result = ScanResult()

        # 1. Get workspace list from storage.json
        storage = _read_json(STORAGE_JSON)
        if not storage:
            log.warning("Cannot read storage.json")
            return result

        folders = storage.get("backupWorkspaces", {}).get("folders", [])
        
        for folder_info in folders:
            uri = folder_info.get("folderUri", "")
            if not uri:
                continue
            path = _uri_to_path(uri)
            name = os.path.basename(path) or path
            ws = Workspace(name=name, path=path, folder_uri=uri)
            
            # 2. Find current conversation for this workspace
            self._load_conversations(ws)
            
            result.workspaces.append(ws)

        # 3. Also scan all todos files for conversations not yet linked
        self._scan_all_todos(result)

        result.scan_time = time.time() - t0
        self._last_result = result
        log.info(f"Scan complete: {len(result.workspaces)} workspaces, "
                 f"{sum(len(ws.conversations) for ws in result.workspaces)} conversations, "
                 f"took {result.scan_time*1000:.0f}ms")
        return result

    def _load_conversations(self, ws: Workspace):
        """Load conversations for a workspace via genie-history."""
        # Try to find the genie-history directory for this workspace
        # by scanning all directories and matching
        if not os.path.isdir(HISTORY_DIR):
            return

        # Approach: scan all genie-history dirs and decode their base64 names
        for dirname in os.listdir(HISTORY_DIR):
            current_path = os.path.join(HISTORY_DIR, dirname, "current.json")
            if not os.path.isfile(current_path):
                continue
            
            # Decode base64 dirname to check if it matches this workspace
            try:
                # Restore padding
                b64 = dirname.replace('_', '=')
                # Sometimes trailing == becomes __ 
                decoded = base64.urlsafe_b64decode(b64 + '==').decode('utf-8', errors='ignore')
                decoded = decoded.rstrip('\x00')  # strip null padding
            except Exception:
                continue

            # Normalize for comparison
            ws_norm = ws.path.replace('\\', '/').lower().rstrip('/')
            dec_norm = decoded.replace('\\', '/').lower().rstrip('/')
            
            if ws_norm != dec_norm:
                continue

            # Found matching history dir
            current = _read_json(current_path)
            if not current:
                continue
            
            conv_id = current.get("conversationId", "")
            if not conv_id:
                continue

            # Load todos for this conversation
            conv = self._load_todos(conv_id)
            if conv:
                ws.conversations.append(conv)

    def _load_todos(self, conv_id: str) -> Optional[Conversation]:
        """Load todos for a specific conversation ID."""
        todo_path = os.path.join(TODOS_DIR, f"{conv_id}.json")
        data = _read_json(todo_path)
        if not data:
            return None

        todos = []
        for item in data.get("todos", []):
            todos.append(TodoItem(
                id=item.get("id", ""),
                status=item.get("status", "pending"),
                content=item.get("content", "")
            ))

        return Conversation(
            conversation_id=conv_id,
            todos=todos,
            updated_at=data.get("updatedAt", 0)
        )

    def _scan_all_todos(self, result: ScanResult):
        """Scan all todo files and attach orphaned ones to a virtual workspace."""
        if not os.path.isdir(TODOS_DIR):
            return

        # Collect conversation IDs already linked to workspaces
        linked_ids = set()
        for ws in result.workspaces:
            for conv in ws.conversations:
                linked_ids.add(conv.conversation_id)

        # Scan todo directory for unlinked conversations
        orphaned = []
        for filename in os.listdir(TODOS_DIR):
            if not filename.endswith('.json'):
                continue
            conv_id = filename[:-5]
            if conv_id in linked_ids:
                continue
            conv = self._load_todos(conv_id)
            if conv and conv.todos:
                orphaned.append(conv)

        # Try to match orphaned conversations to workspaces via genie-history
        # by scanning all history dirs
        still_orphaned = []
        for conv in orphaned:
            matched = False
            if os.path.isdir(HISTORY_DIR):
                for dirname in os.listdir(HISTORY_DIR):
                    current_path = os.path.join(HISTORY_DIR, dirname, "current.json")
                    current = _read_json(current_path)
                    if current and current.get("conversationId") == conv.conversation_id:
                        # Decode dirname to find workspace path
                        try:
                            b64 = dirname.replace('_', '=')
                            decoded = base64.urlsafe_b64decode(b64 + '==').decode('utf-8', errors='ignore').rstrip('\x00')
                        except Exception:
                            decoded = dirname
                        
                        # Find matching workspace
                        for ws in result.workspaces:
                            ws_norm = ws.path.replace('\\', '/').lower().rstrip('/')
                            dec_norm = decoded.replace('\\', '/').lower().rstrip('/')
                            if ws_norm == dec_norm:
                                ws.conversations.append(conv)
                                matched = True
                                break
                        break
            if not matched:
                still_orphaned.append(conv)

        # If there are still orphaned conversations, put them in a catch-all workspace
        if still_orphaned:
            orphan_ws = Workspace(name="(其他对话)", path="", folder_uri="")
            orphan_ws.conversations = still_orphaned
            result.workspaces.append(orphan_ws)

    @property
    def last_result(self) -> Optional[ScanResult]:
        return self._last_result


def to_wire_workspaces(result: ScanResult) -> list[dict]:
    """Convert scan result to workspace list for serial protocol (MSG_WORKSPACE_LIST)."""
    ws_list = []
    for i, ws in enumerate(result.workspaces):
        task_count = sum(len(c.todos) for c in ws.conversations)
        completed = sum(1 for c in ws.conversations for t in c.todos if t.status == "completed")
        in_progress = sum(1 for c in ws.conversations for t in c.todos if t.status == "in_progress")
        ws_list.append({
            "id": i,
            "name": ws.name,
            "tasks": task_count,
            "done": completed,
            "active": in_progress,
        })
    return ws_list


def to_wire_tasks(ws: Workspace) -> list[dict]:
    """Convert workspace's tasks to task list for serial protocol (MSG_TASK_LIST)."""
    tasks = []
    for conv in ws.conversations:
        for todo in conv.todos:
            # Status shorthand: c=completed, p=in_progress, d=pending
            s = {"completed": "c", "in_progress": "p", "pending": "d"}.get(todo.status, "d")
            tasks.append({
                "id": todo.id,
                "cid": conv.conversation_id[:8],  # Short conv ID for display
                "s": s,
                "c": todo.content[:180],  # Truncate for serial (192 byte buffer on Core2)
            })
    return tasks


# ── Quick test ──────────────────────────────────────────────────────────────
if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    scanner = WorkBuddyScanner()
    result = scanner.scan()
    
    import sys
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    
    print(f"\n{'='*60}")
    print(f"Found {len(result.workspaces)} workspaces:")
    for ws in result.workspaces:
        print(f"\n  [WS] {ws.name} ({ws.path})")
        for conv in ws.conversations:
            print(f"     [CONV] {conv.conversation_id[:8]}... ({len(conv.todos)} todos, updated {conv.updated_at})")
            for todo in conv.todos:
                icon = {"completed": "[OK]", "in_progress": "[..] ", "pending": "[  ]"}.get(todo.status, "[??]")
                print(f"        {icon} [{todo.id}] {todo.content}")
    
    print(f"\n{'='*60}")
    print(f"Wire format (workspaces):")
    print(json.dumps(to_wire_workspaces(result), ensure_ascii=False, indent=2))
