test_run = require('test_run').new()
fiber = require('fiber')
fio = require('fio')

test_run:cleanup_cluster()

-- Make each snapshot trigger garbage collection.
default_checkpoint_count = box.cfg.checkpoint_count
box.cfg{checkpoint_count = 1}

-- Temporary space for bumping lsn.
temp = box.schema.space.create('temp')
_ = temp:create_index('pk')

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk', {run_count_per_level=1})

path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id))

function ls_data() return fio.glob(fio.pathjoin(path, '*')) end
function ls_vylog() return fio.glob(fio.pathjoin(box.cfg.vinyl_dir, '*.vylog')) end
function gc_info() return box.internal.gc.info() end
function gc() temp:auto_increment{} box.snapshot() end

-- Check that run files are deleted by gc.
s:insert{1} box.snapshot() -- dump
s:insert{2} box.snapshot() -- dump + compaction
while s.index.pk:info().run_count > 1 do fiber.sleep(0.01) end -- wait for compaction
gc()
files = ls_data()
#files == 2 or {files, gc_info()}

-- Check that gc keeps the current and previous log files.
files = ls_vylog()
#files == 2 or {files, gc_info()}

-- Check that files left from dropped indexes are deleted by gc.
s:drop()
gc()
files = ls_data()
#files == 0 or {files, gc_info()}

--
-- Check that vylog files are removed if vinyl is not used.
--

files = ls_vylog()
#files == 2 or {files, gc_info()}

-- All records should have been purged from the log by now
-- so we should only keep the previous log file.
gc()
files = ls_vylog()
#files == 1 or {files, gc_info()}

-- The previous log file should be removed by the next gc.
gc()
files = ls_vylog()
#files == 0 or {files, gc_info()}

temp:drop()

box.cfg{checkpoint_count = default_checkpoint_count}
