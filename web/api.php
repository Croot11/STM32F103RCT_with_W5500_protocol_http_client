<?php
/**
 * Relay + Sensor Control API
 *
 * GET  /api.php              → trả về trạng thái tất cả relay (JSON)
 * GET  /api.php?id=1         → trả về trạng thái relay #1
 * POST /api.php              → cập nhật trạng thái relay
 *      Body: {"id":1,"state":true}
 *
 * === SENSOR / NHIỆT ĐỘ ===
 * GET  /api.php?action=sensor           → lấy dữ liệu cảm biến mới nhất
 * GET  /api.php?action=sensor_history   → lấy lịch sử (tối đa 60 điểm)
 * POST /api.php?action=sensor           → mạch gửi dữ liệu lên
 *      Body: {"temperature":28.5,"humidity":65.2,"device":"STM32"}
 *      (humidity và device là tuỳ chọn)
 */

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { exit; }

// ─── File lưu trữ ────────────────────────────────────────────────────────────
define('STATE_FILE',   __DIR__ . '/relay_state.json');
define('SENSOR_FILE',  __DIR__ . '/sensor_latest.json');
define('HISTORY_FILE', __DIR__ . '/sensor_history.json');

// Số điểm lịch sử tối đa lưu lại
define('MAX_HISTORY', 60);

// ─── Relay helpers ────────────────────────────────────────────────────────────
function readState() {
    if (!file_exists(STATE_FILE)) {
        $default = [
            ["id" => 1, "name" => "Relay 1", "state" => false],
            ["id" => 2, "name" => "Relay 2", "state" => false],
            ["id" => 3, "name" => "Relay 3", "state" => false],
            ["id" => 4, "name" => "Relay 4", "state" => false],
        ];
        file_put_contents(STATE_FILE, json_encode($default));
        return $default;
    }
    return json_decode(file_get_contents(STATE_FILE), true);
}

function writeState($data) {
    file_put_contents(STATE_FILE, json_encode($data));
}

// ─── Sensor helpers ───────────────────────────────────────────────────────────
function readSensorLatest() {
    if (!file_exists(SENSOR_FILE)) return null;
    return json_decode(file_get_contents(SENSOR_FILE), true);
}

function readSensorHistory() {
    if (!file_exists(HISTORY_FILE)) return [];
    return json_decode(file_get_contents(HISTORY_FILE), true) ?? [];
}

function writeSensor($entry) {
    // Lưu bản ghi mới nhất
    file_put_contents(SENSOR_FILE, json_encode($entry));

    // Thêm vào lịch sử, giữ tối đa MAX_HISTORY điểm
    $history   = readSensorHistory();
    $history[] = $entry;
    if (count($history) > MAX_HISTORY) {
        $history = array_slice($history, -MAX_HISTORY);
    }
    file_put_contents(HISTORY_FILE, json_encode(array_values($history)));
}

// ─── Router ───────────────────────────────────────────────────────────────────
$action = $_GET['action'] ?? '';

// ══════════ SENSOR ══════════
if ($action === 'sensor') {

    // GET → trả dữ liệu mới nhất
    if ($_SERVER['REQUEST_METHOD'] === 'GET') {
        $latest = readSensorLatest();
        if (!$latest) {
            http_response_code(404);
            echo json_encode(["error" => "No sensor data yet"]);
            exit;
        }
        echo json_encode($latest);
        exit;
    }

    // POST → mạch gửi dữ liệu lên
    if ($_SERVER['REQUEST_METHOD'] === 'POST') {
        $body = json_decode(file_get_contents('php://input'), true);

        if (!isset($body['temperature'])) {
            http_response_code(400);
            echo json_encode(["error" => "Missing temperature field"]);
            exit;
        }

        $entry = [
            "temperature" => round((float)$body['temperature'], 2),
            "humidity"    => isset($body['humidity'])  ? round((float)$body['humidity'], 2)  : null,
            "device"      => $body['device'] ?? 'unknown',
            "timestamp"   => time(),                        // Unix epoch (giây)
            "datetime"    => date('Y-m-d H:i:s'),          // Chuỗi dễ đọc
        ];

        writeSensor($entry);
        echo json_encode(["ok" => true, "received" => $entry]);
        exit;
    }
}

// ══════════ SENSOR HISTORY ══════════
if ($action === 'sensor_history') {
    if ($_SERVER['REQUEST_METHOD'] === 'GET') {
        $limit   = min((int)($_GET['limit'] ?? MAX_HISTORY), MAX_HISTORY);
        $history = readSensorHistory();
        $history = array_slice($history, -$limit);
        echo json_encode(["history" => array_values($history), "count" => count($history)]);
        exit;
    }
}

// ══════════ RELAY ══════════
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    $relays = readState();

    if (isset($_GET['id'])) {
        $id = intval($_GET['id']);
        foreach ($relays as $r) {
            if ($r['id'] === $id) {
                echo json_encode([
                    "id"    => $r['id'],
                    "name"  => $r['name'],
                    "state" => $r['state'],
                    "value" => $r['state'] ? 1 : 0
                ]);
                exit;
            }
        }
        http_response_code(404);
        echo json_encode(["error" => "Relay not found"]);
        exit;
    }

    echo json_encode(["relays" => $relays, "count" => count($relays)]);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $body = json_decode(file_get_contents('php://input'), true);

    if (!isset($body['id']) || !isset($body['state'])) {
        http_response_code(400);
        echo json_encode(["error" => "Missing id or state"]);
        exit;
    }

    $relays = readState();
    $found  = false;

    foreach ($relays as &$r) {
        if ($r['id'] === intval($body['id'])) {
            $r['state'] = (bool)$body['state'];
            $found = true;
            break;
        }
    }

    if (!$found) {
        http_response_code(404);
        echo json_encode(["error" => "Relay not found"]);
        exit;
    }

    writeState($relays);
    echo json_encode(["ok" => true, "id" => $body['id'], "state" => $body['state']]);
    exit;
}

http_response_code(405);
echo json_encode(["error" => "Method not allowed"]);
