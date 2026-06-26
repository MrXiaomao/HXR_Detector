clc; clear;

folderPath = uigetdir(pwd, '选择二进制数据文件夹');
if folderPath == 0
    disp('未选择文件夹');
    return;
end

files = dir(fullfile(folderPath, '*.dat'));

outExcel = fullfile(folderPath, 'temperature_abnormal_all.xlsx');

packetLen = 20;
unixOffset = 2208988800;

abnormal = {};
normal = {};
normalMap = containers.Map();
fileSummary = {};

globalPacketIndex = 0;

for f = 1:length(files)

    fileName = files(f).name;
    filePath = fullfile(folderPath, fileName);

    fid = fopen(filePath, 'rb');
    data = fread(fid, inf, 'uint8=>uint8');
    fclose(fid);

    prevSec = [];
    i = 1;

    fileTempCount = 0;
    lastSec1900 = NaN;
    lastFrac32 = NaN;
    lastUtcTime = NaT;
    lastBjTime = NaT;

    while i <= length(data) - packetLen + 1

        isTempPacket = ...
            data(i)     == hex2dec('12') && ...
            data(i + 1) == hex2dec('34') && ...
            data(i + 2) == hex2dec('00') && ...
            data(i + 3) == hex2dec('0A') && ...
            data(i + 4) == hex2dec('DA') && ...
            data(i + 5) == hex2dec('11') && ...
            data(i + 18)== hex2dec('AB') && ...
            data(i + 19)== hex2dec('CD');

        if ~isTempPacket
            i = i + 1;
            continue;
        end

        globalPacketIndex = globalPacketIndex + 1;
        fileTempCount = fileTempCount + 1;

        sec1900 = double(data(i+6))  * 2^24 + ...
                  double(data(i+7))  * 2^16 + ...
                  double(data(i+8))  * 2^8  + ...
                  double(data(i+9));

        frac32 = double(data(i+10)) * 2^24 + ...
                 double(data(i+11)) * 2^16 + ...
                 double(data(i+12)) * 2^8  + ...
                 double(data(i+13));

        tempU32 = uint32(data(i+14)) * uint32(2^24) + ...
                  uint32(data(i+15)) * uint32(2^16) + ...
                  uint32(data(i+16)) * uint32(2^8)  + ...
                  uint32(data(i+17));

        tempRaw = typecast(tempU32, 'int32');
        temperature = double(tempRaw) / 10000.0;

        unixSec = sec1900 - unixOffset;
        fracSec = frac32 / 2^32;

        utcTime = datetime(unixSec + fracSec, ...
            'ConvertFrom', 'posixtime', ...
            'TimeZone', 'UTC');

        bjTime = utcTime;
        bjTime.TimeZone = 'Asia/Shanghai';

        reason = "";

        if ~isempty(prevSec)
            diffSec = sec1900 - prevSec;

            if diffSec == 0
                reason = reason + "重复时间戳;";
            elseif diffSec < 0
                reason = reason + "时间戳倒退;";
            elseif diffSec > 1
                reason = reason + sprintf( ...
                    "疑似丢包，间隔 %.0f 秒，预计丢失 %.0f 个心跳包;", ...
                    diffSec, diffSec - 1);
            end
        else
            diffSec = NaN;
        end

        row = {fileName, globalPacketIndex, i-1, sec1900, frac32, ...
               char(utcTime), char(bjTime), ...
               double(tempRaw), temperature, diffSec, char(reason)};

        if strlength(reason) > 0
            abnormal = [abnormal; row];
        else
            normalRow = {fileTempCount, char(bjTime), diffSec};

            if isKey(normalMap, fileName)
                normalMap(fileName) = [normalMap(fileName); normalRow];
            else
                normalMap(fileName) = normalRow;
            end
        end

        prevSec = sec1900;

        lastSec1900 = sec1900;
        lastFrac32 = frac32;
        lastUtcTime = utcTime;
        lastBjTime = bjTime;

        i = i + packetLen;
    end

    if fileTempCount > 0
        fileSummary = [fileSummary; ...
            {fileName, fileTempCount, lastSec1900, lastFrac32, ...
             char(lastUtcTime), char(lastBjTime), ...
             year(lastBjTime), month(lastBjTime), day(lastBjTime), ...
             hour(lastBjTime), minute(lastBjTime), second(lastBjTime)}];
    else
        fileSummary = [fileSummary; ...
            {fileName, 0, NaN, NaN, '', '', ...
             NaN, NaN, NaN, NaN, NaN, NaN}];
    end
end

header = {'文件名','全局包序号','文件偏移_从0开始','UTC整数秒_1900', ...
          'UTC小数秒原始值','UTC时间','北京时间', ...
          '温度原始值','温度_℃','与上一包秒差','异常原因'};

summaryHeader = {'文件名','温度包数量','最后UTC整数秒_1900','最后UTC小数秒原始值', ...
                 '最后UTC时间','最后北京时间', ...
                 '年','月','日','时','分','秒'};

writecell([header; abnormal], outExcel, 'Sheet', '异常温度包');
writecell([summaryHeader; fileSummary], outExcel, 'Sheet', '每个文件最后时间');
normalHeader = {'能谱序号','UTC北京时间','与上一包秒差'};
fileKeys = keys(normalMap);
for k = 1:length(fileKeys)
    thisFileName = fileKeys{k};
    thisData = normalMap(thisFileName);

    [~, nameOnly, ~] = fileparts(thisFileName);

    sheetName = nameOnly;

    % Excel sheet 名最多31个字符
    if strlength(sheetName) > 31
        sheetName = extractBefore(sheetName, 32);
    end

    writecell([normalHeader; thisData], outExcel, 'Sheet', sheetName);
end

fprintf('分析完成。\n');
fprintf('分析文件数量：%d\n', length(files));
fprintf('正常温度包数量：%d\n', size(normal, 1));
fprintf('异常温度包数量：%d\n', size(abnormal, 1));
fprintf('Excel已保存到：%s\n', outExcel);