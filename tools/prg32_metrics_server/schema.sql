PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS runs (
    run_id TEXT PRIMARY KEY,
    board_id TEXT NOT NULL,
    target TEXT NOT NULL,
    display_backend TEXT NOT NULL DEFAULT '',
    firmware_version TEXT NOT NULL DEFAULT '',
    firmware_git_sha TEXT NOT NULL DEFAULT '',
    game_name TEXT NOT NULL DEFAULT '',
    sample_period_frames INTEGER NOT NULL DEFAULT 1,
    started_at INTEGER NOT NULL,
    finished_at INTEGER,
    dropped_samples INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

CREATE TABLE IF NOT EXISTS samples (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id TEXT NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,
    frame INTEGER NOT NULL,
    timestamp_ms INTEGER NOT NULL,
    update_us INTEGER NOT NULL,
    draw_us INTEGER NOT NULL,
    present_us INTEGER NOT NULL,
    frame_us INTEGER NOT NULL,
    heap_free INTEGER NOT NULL,
    heap_min_free INTEGER NOT NULL,
    input_mask INTEGER NOT NULL,
    fps_x100 INTEGER NOT NULL,
    upload_queue_depth INTEGER NOT NULL,
    deadline_missed INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    UNIQUE(run_id, frame)
);

CREATE INDEX IF NOT EXISTS samples_run_frame_idx ON samples(run_id, frame);
CREATE INDEX IF NOT EXISTS samples_run_frame_time_idx ON samples(run_id, frame_us);
