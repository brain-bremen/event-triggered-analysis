# Loading a session in MATLAB

MATLAB reads the XML half natively. It has no built-in `.npy` reader, so this page gives
a short one — about forty lines, no toolbox, no MEX, no download.

Save the two functions below as `readNPY.m` and `loadEventTriggeredSession.m` somewhere on
your path.

!!! warning "C order versus column-major"

    NumPy arrays are **row-major**; MATLAB is **column-major**. Reading the bytes straight
    into a `reshape` of the stored shape silently transposes the array — and a
    `(sources, channels, samples)` array read that way is not obviously wrong, it just has
    the wrong trace in it. `readNPY` below reshapes into the *reversed* shape and then
    permutes, which is the correction.

    After it, an array documented as `(sources, channels, samples)` is
    `(sources, channels, samples)` in MATLAB too, indexed `a(source, channel, sample)`.

## `readNPY.m`

```matlab
function [data, shape] = readNPY(filename)
%READNPY Read a NumPy .npy array written by the Event-Triggered Analysis plugins.
%
%   [DATA, SHAPE] = READNPY(FILENAME) returns the array in the same dimension
%   order NumPy reports, i.e. an array documented as (sources, channels,
%   samples) is indexed DATA(source, channel, sample).
%
%   Supports .npy format 1.0 and 2.0, C order, little-endian float32/float64/
%   int32/int64 — everything these plugins write. Anything else is an error
%   rather than a silent conversion.

    fid = fopen(filename, 'r', 'l');       % little-endian, as the dtype says
    if fid < 0
        error('readNPY:open', 'Could not open %s', filename);
    end
    closer = onCleanup(@() fclose(fid));

    magic = fread(fid, 6, '*uint8')';
    if ~isequal(magic, uint8([147 78 85 77 80 89]))   % \x93NUMPY
        error('readNPY:magic', '%s is not a .npy file', filename);
    end

    major = fread(fid, 1, 'uint8');
    fread(fid, 1, 'uint8');                            % minor, unused
    if major == 1
        headerLength = fread(fid, 1, 'uint16');
    else
        headerLength = fread(fid, 1, 'uint32');
    end

    header = fread(fid, headerLength, '*char')';

    % --- the header is a Python dict literal; three fields matter -----------
    descr = regexp(header, '''descr''\s*:\s*''([^'']+)''', 'tokens', 'once');
    order = regexp(header, '''fortran_order''\s*:\s*(\w+)', 'tokens', 'once');
    dims  = regexp(header, '''shape''\s*:\s*\(([^)]*)\)', 'tokens', 'once');

    if isempty(descr) || isempty(dims)
        error('readNPY:header', 'Could not parse the .npy header of %s', filename);
    end

    % Fortran order is rejected rather than treated as C order: nothing here
    % writes it, and reading one as C order transposes the array.
    if ~isempty(order) && strcmpi(order{1}, 'True')
        error('readNPY:fortran', '%s is Fortran-ordered', filename);
    end

    shape = sscanf(dims{1}, '%d,')';                   % "(2, 3, 4)" -> [2 3 4]
    if isempty(shape)
        shape = 1;                                     % zero-dimensional
    end

    switch descr{1}
        case '<f4', precision = '*single';
        case '<f8', precision = '*double';
        case '<i4', precision = '*int32';
        case '<i8', precision = '*int64';
        otherwise
            error('readNPY:dtype', 'Unsupported dtype %s in %s', descr{1}, filename);
    end

    data = fread(fid, prod(shape), precision);

    if numel(data) ~= prod(shape)
        error('readNPY:truncated', '%s is shorter than its header says', filename);
    end

    % NumPy is row-major and MATLAB is column-major, so fill the reversed shape
    % and permute back. For a vector both are the same and permute is a no-op.
    if numel(shape) > 1
        data = permute(reshape(data, fliplr(shape)), numel(shape):-1:1);
    else
        data = reshape(data, [shape 1]);
    end
end
```

## `loadEventTriggeredSession.m`

```matlab
function session = loadEventTriggeredSession(directory)
%LOADEVENTTRIGGEREDSESSION Read a session directory written by the
%   Event-Triggered Analysis plugins.
%
%   S = LOADEVENTTRIGGEREDSESSION(DIR) parses DIR/session.xml and loads every
%   array listed in it. Fields:
%
%       plugin, pluginVersion, savedAt, isDemoData
%       sampleRateHz, preSamples, postSamples, numSamples
%       channels    struct array: index, name          (accumulator order)
%       conditions  struct array: name, line, colour,
%                   armPattern, cancelPattern, commitPattern,
%                   pendingTimeoutMs, angleDeg          (array first-axis order)
%       arrays      struct: one field per array, e.g. session.arrays.averages
%       timeMs      trial time axis in ms, trigger at 0
%       attributes  every root attribute, as a struct of char arrays
%
%   Uses xmlread rather than readstruct so it works on any MATLAB release.

    % char() rather than an arguments block, so a "double-quoted" string works
    % too and the function loads on releases before R2019b.
    directory = char(directory);

    manifest = fullfile(directory, 'session.xml');
    if ~isfile(manifest)
        error('session:missing', '%s is not a session directory', directory);
    end

    root = xmlread(manifest).getDocumentElement();
    if ~strcmp(char(root.getTagName()), 'EVENT_TRIGGERED_SESSION')
        error('session:manifest', '%s is not a session manifest', manifest);
    end

    version = str2double(attr(root, 'format_version', '0'));
    if version ~= 1
        error('session:version', 'Unsupported session format_version %g', version);
    end

    session = struct();
    session.directory = directory;

    % --- every root attribute, verbatim, so plugin-specific ones are reachable
    session.attributes = struct();
    named = root.getAttributes();
    for i = 0:named.getLength() - 1
        item = named.item(i);
        session.attributes.(matlab.lang.makeValidName(char(item.getName()))) = ...
            char(item.getValue());
    end

    % --- provenance --------------------------------------------------------
    session.plugin        = attr(root, 'plugin', '');
    session.pluginVersion = attr(root, 'plugin_version', '');
    session.savedAt       = attr(root, 'saved_at', '');
    session.isDemoData    = strcmp(attr(root, 'demo_data', '0'), '1');

    % --- trial geometry ----------------------------------------------------
    session.sampleRateHz = str2double(attr(root, 'sample_rate_hz', '0'));
    session.preSamples   = str2double(attr(root, 'pre_samples', '0'));
    session.postSamples  = str2double(attr(root, 'post_samples', '0'));
    session.numSamples   = session.preSamples + session.postSamples;

    % --- channels, in accumulator order ------------------------------------
    session.channels = struct('index', {}, 'name', {});
    channelNodes = elements(root, 'CHANNEL');
    for i = 1:numel(channelNodes)
        session.channels(i).index = str2double(attr(channelNodes{i}, 'index', '-1'));
        session.channels(i).name  = attr(channelNodes{i}, 'name', '');
    end

    % --- conditions, in the arrays' first-axis order -----------------------
    % Sweep angles (Bar Mapper) are a parallel list matched by position, not
    % attributes on the sources, so they are collected separately and zipped.
    angleNodes = elements(root, 'SWEEPANGLE');
    angles = nan(1, numel(angleNodes));
    for i = 1:numel(angleNodes)
        index = str2double(attr(angleNodes{i}, 'index', '-1'));
        if index >= 0 && angleNodes{i}.hasAttribute('angleDeg')
            angles(index + 1) = str2double(attr(angleNodes{i}, 'angleDeg', 'NaN'));
        end
    end

    session.conditions = struct('name', {}, 'line', {}, 'colour', {}, ...
                                'armPattern', {}, 'cancelPattern', {}, ...
                                'commitPattern', {}, 'pendingTimeoutMs', {}, ...
                                'angleDeg', {});

    sourceNodes = elements(root, 'TRIGGERSOURCE');
    for i = 1:numel(sourceNodes)
        node = sourceNodes{i};
        session.conditions(i).name             = attr(node, 'name', '');
        session.conditions(i).line             = str2double(attr(node, 'line', '-1'));
        session.conditions(i).colour           = attr(node, 'colour', '');
        session.conditions(i).armPattern       = attr(node, 'armPattern', '');
        session.conditions(i).cancelPattern    = attr(node, 'cancelPattern', '');
        session.conditions(i).commitPattern    = attr(node, 'commitPattern', '');
        session.conditions(i).pendingTimeoutMs = str2double(attr(node, 'pendingTimeoutMs', '5000'));

        if i <= numel(angles)
            session.conditions(i).angleDeg = angles(i);   % NaN = no angle set
        else
            session.conditions(i).angleDeg = NaN;
        end
    end

    % --- arrays ------------------------------------------------------------
    session.arrays = struct();
    arrayNodes = elements(root, 'ARRAY');
    for i = 1:numel(arrayNodes)
        name = attr(arrayNodes{i}, 'name', '');
        file = attr(arrayNodes{i}, 'file', '');
        session.arrays.(matlab.lang.makeValidName(name)) = ...
            readNPY(fullfile(directory, strrep(file, '/', filesep)));
    end

    % --- the time axis, read rather than recomputed ------------------------
    if isfield(session.arrays, 'time_ms')
        session.timeMs = double(session.arrays.time_ms(:))';
    else
        n = 0:session.numSamples - 1;
        session.timeMs = 1000 * (n - session.preSamples) / session.sampleRateHz;
    end
end

% --- helpers ---------------------------------------------------------------

function value = attr(node, name, fallback)
    if node.hasAttribute(name)
        value = char(node.getAttribute(name));
    else
        value = fallback;
    end
end

function nodes = elements(root, tag)
    list = root.getElementsByTagName(tag);
    nodes = cell(1, list.getLength());
    for i = 1:list.getLength()
        nodes{i} = list.item(i - 1);
    end
end
```

## Working with it

```matlab
s = loadEventTriggeredSession('/data/sessions/TriggeredAvg_2026-08-24_143107');

fprintf('%s %s, saved %s\n', s.plugin, s.pluginVersion, s.savedAt);
fprintf('%d conditions, %d channels, %g Hz\n', ...
        numel(s.conditions), numel(s.channels), s.sampleRateHz);

averages = s.arrays.averages;        % (sources, channels, samples)
counts   = double(s.arrays.trial_counts);
t        = s.timeMs;

for i = 1:numel(s.conditions)
    fprintf('  %-12s %4d trials\n', s.conditions(i).name, counts(i));
end
```

### Standard error of the mean

`standard_deviations` is the **population** standard deviation over trials. Divide by the
square root of the trial count — and guard against a condition with no trials, which is
written as zeros rather than NaN:

```matlab
sd = double(s.arrays.standard_deviations);
n  = double(s.arrays.trial_counts(:));

sem = nan(size(sd));
for i = 1:numel(n)
    if n(i) > 0
        sem(i, :, :) = sd(i, :, :) / sqrt(n(i));
    end
end
```

### Rebuilding the average from the resumable state

`averages` is written for convenience but is derived; `sums` and `trial_counts` are the
state:

```matlab
sums = double(s.arrays.sums);
n    = double(s.arrays.trial_counts(:));

averages = zeros(size(sums));
for i = 1:numel(n)
    if n(i) > 0
        averages(i, :, :) = sums(i, :, :) / n(i);
    end
end
```

### Selecting a condition by name

Never by an index typed in by hand — the order is the trigger table's, and it changes when
a condition is added:

```matlab
names = {s.conditions.name};
i = find(strcmp(names, 'Attend in'), 1);

trace = squeeze(s.arrays.averages(i, 1, :));   % first channel of that condition
```

### Plotting an evoked average

```matlab
s = loadEventTriggeredSession('/data/sessions/TriggeredAvg_2026-08-24_143107');

averages = double(s.arrays.averages);
sd       = double(s.arrays.standard_deviations);
n        = double(s.arrays.trial_counts(:));
t        = s.timeMs;

channel = 1;
figure; hold on

for i = 1:numel(s.conditions)
    if n(i) == 0
        continue
    end

    mu  = squeeze(averages(i, channel, :))';
    sem = squeeze(sd(i, channel, :))' / sqrt(n(i));

    h = plot(t, mu, 'LineWidth', 1.2, ...
             'DisplayName', sprintf('%s (n=%d)', s.conditions(i).name, n(i)));
    fill([t fliplr(t)], [mu - sem, fliplr(mu + sem)], h.Color, ...
         'FaceAlpha', 0.2, 'EdgeColor', 'none', 'HandleVisibility', 'off');
end

xline(0, '--', 'Color', [0.4 0.4 0.4]);
xlabel('Time from trigger (ms)')
ylabel('Amplitude (\muV)')
title(sprintf('%s — %s', s.channels(channel).name, s.plugin))
legend show
```

## Receptive-field sessions

The Bar Mapper writes everything above — its conditions are directions — plus the finished
maps.

```matlab
s = loadEventTriggeredSession('/data/sessions/RFBarMapper_2026-08-24_151122');

maps      = double(s.arrays.maps);                 % (channels, pixels, pixels)
estimates = double(s.arrays.map_estimates);        % (channels, 7)
valid     = logical(s.arrays.map_valid);
indices   = double(s.arrays.map_channel_indices);

% Read the column names from the manifest rather than hard-coding the order.
fieldNames = strsplit(s.attributes.map_estimate_fields, ',');
estimate = cell2struct(num2cell(estimates(1, :))', fieldNames', 1);

fprintf('RF %.2f deg at (%.2f, %.2f)\n', ...
        estimate.equivalent_diameter_deg, ...
        estimate.centre_x_deg, estimate.centre_y_deg);
```

### Plotting a map

```matlab
pixels      = str2double(s.attributes.map_pixels);
degPerPixel = str2double(s.attributes.map_degrees_per_pixel);
centreX     = str2double(s.attributes.map_centre_x_deg);
centreY     = str2double(s.attributes.map_centre_y_deg);

half = 0.5 * pixels * degPerPixel;
xs = linspace(centreX - half, centreX + half, pixels);
ys = linspace(centreY - half, centreY + half, pixels);

channel = 1;
figure
imagesc(xs, ys, squeeze(maps(channel, :, :)))
axis image
colormap(jet)
colorbar
set(gca, 'YDir', 'reverse')   % map rows run top to bottom; see below
xlabel('Visual field x (deg)')
ylabel('Visual field y (deg)')
title(sprintf('%s — RF %.2f deg', s.channels(channel).name, estimate.equivalent_diameter_deg))
```

!!! warning "Rows run top to bottom, visual-field *y* runs upwards"

    The flip is applied once, when the map is built. `imagesc` already draws the first row
    at the top (`YDir` is `'reverse'` by default for images), so the picture matches the
    canvas — the explicit `set` above is only there to survive a figure whose default was
    changed.

### The per-direction traces behind the map

The map is a combination of the direction averages, and when the map looks wrong the cause
is usually visible in those:

```matlab
angles = [s.conditions.angleDeg];
counts = double(s.arrays.trial_counts);

[~, order] = sort(angles);
for i = order
    if isnan(angles(i))
        fprintf('  (no angle)  n=%4d  %s\n', counts(i), s.conditions(i).name);
    else
        fprintf('  %6.1f deg   n=%4d  %s\n', angles(i), counts(i), s.conditions(i).name);
    end
end
```

A direction with **no angle contributes nothing** to the map — `angleDeg` is `NaN`, not
`0`, and that distinction is the point.

## Alternatives to `xmlread`

On R2020b and later, `readstruct` parses the manifest in one line:

```matlab
manifest = readstruct(fullfile(directory, 'session.xml'), 'FileType', 'xml');
fs = manifest.sample_rate_hzAttribute;
```

Attributes get an `Attribute` suffix (configurable with the `AttributeSuffix` name-value
argument), and the result is a struct rather than a DOM. It is shorter, but the field
naming depends on the release, which is why the loader above uses `xmlread` instead.

## Things worth knowing

| | |
|---|---|
| **C order versus column-major** | `readNPY` permutes for you. A reader that does not will transpose the array without complaining. |
| **A condition with no trials is zeros, not NaN** | `trial_counts` is what distinguishes it from a real zero. Check it before dividing. |
| **`demo_data="1"` means simulated data** | `s.isDemoData`. It exists so demo data can never be mistaken for a recording. |
| **The parameter values are not in the session** | Only the trial geometry, the channel list, the trigger table and — for the Bar Mapper — the map geometry and sweep angles. See [Format](format.md#custom_parameters). |
| **`readNPY` returns the stored integer type** | `trial_counts` comes back as `int32`. Cast with `double()` before arithmetic, or integer division will bite. |
