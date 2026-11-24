#include "portability.hpp"
#include <miniz.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace pvl {
namespace {
constexpr auto odsType = "application/vnd.oasis.opendocument.spreadsheet";
constexpr auto xlsxType = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
constexpr std::size_t exportLimit = 8 * 1024 * 1024;

class WorkbookZip {
    mz_zip_archive zip{};
public:
    WorkbookZip() {
        if (!mz_zip_writer_init_heap(&zip, 0, 0)) throw std::runtime_error("Cannot create workbook");
    }
    ~WorkbookZip() { mz_zip_writer_end(&zip); }
    void add(const char* name, std::string_view content, int compression = MZ_BEST_SPEED) {
        const auto read = [](void* source, mz_uint64 offset, void* dest, size_t count) -> size_t {
            const auto& bytes = *static_cast<const std::string_view*>(source);
            if (offset >= bytes.size()) return 0;
            count = std::min(count, bytes.size() - static_cast<size_t>(offset));
            std::memcpy(dest, bytes.data() + offset, count);
            return count;
        };
        // Calc reads the ODS mimetype from its local ZIP header and requires its size there.
        if (!mz_zip_writer_add_read_buf_callback(&zip, name, read, &content, content.size(), nullptr, nullptr, 0,
                compression | MZ_ZIP_FLAG_WRITE_HEADER_SET_SIZE, nullptr, 0, nullptr, 0))
            throw std::runtime_error("Cannot write workbook");
    }
    std::string finish() {
        void* buffer = nullptr;
        std::size_t size = 0;
        if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size))
            throw std::runtime_error("Cannot finish workbook");
        std::unique_ptr<void, decltype(&mz_free)> owner(buffer, mz_free);
        if (size > exportLimit) throw Error(422, "narrow_filters");
        return {static_cast<const char*>(buffer), size};
    }
};

// XML 1.0 permits Unicode scalar values except for certain control characters and U+FFFE/U+FFFF.
void validateCellText(std::string_view text, bool ods) {
    size_t units = 0;
    for (size_t i = 0; i < text.size();) {
        unsigned code = static_cast<unsigned char>(text[i++]);
        if (code >= 0x80) {
            if (code < 0xc2 || code > 0xf4) throw Error(422, "spreadsheet_text");
            const unsigned trailing = code < 0xe0 ? 1 : code < 0xf0 ? 2 : 3;
            if (text.size() - i < trailing) throw Error(422, "spreadsheet_text");
            code &= 0x7f >> (trailing + 1);
            for (unsigned j = 0; j < trailing; ++j) {
                const auto next = static_cast<unsigned char>(text[i++]);
                if ((next & 0xc0) != 0x80) throw Error(422, "spreadsheet_text");
                code = (code << 6) | (next & 0x3f);
            }
            if ((trailing == 2 && code < 0x800) || (trailing == 3 && code < 0x10000) ||
                (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff || code == 0xfffe || code == 0xffff)
                throw Error(422, "spreadsheet_text");
        } else if (code < 32 && code != '\t' && code != '\n' && code != '\r') {
            throw Error(422, "spreadsheet_text");
        }
        units += code > 0xffff ? 2 : 1;
    }
    if (!ods && units > 32767) throw Error(422, "spreadsheet_text");
}

std::string cellText(const std::string& value, bool ods) {
    validateCellText(value, ods);
    // Preserve whitespace through XML/ODF normalization and literal OOXML escape sequences.
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto ch = static_cast<unsigned char>(value[i]);
        if (ods && ch == ' ') out += "<text:s/>";
        else if (ods && ch == '\t') out += "<text:tab/>";
        else if (ods && ch == '\n') out += "<text:line-break/>";
        else if (ch == '\r') out += "&#13;";
        else if (ch == '&') out += "&amp;";
        else if (ch == '<') out += "&lt;";
        else if (ch == '>') out += "&gt;";
        else if (!ods && value.compare(i, 2, "_x") == 0 && i + 6 < value.size() &&
                 value[i + 6] == '_' && value.find_first_not_of("0123456789ABCDEFabcdef", i + 2) == i + 6)
            out += "_x005F_";
        else out += value[i];
    }
    return out;
}

std::string cell(const Value& value, bool ods) {
    if (value.isNull()) return ods ? "<table:table-cell/>" : "<c/>";
    // Spreadsheet readers round long integer identifiers when stored as numbers.
    const bool longInteger = value.type() == Json::intValue && std::abs(value.asDouble()) >= 1e15;
    if (value.isNumeric() && !longInteger) {
        if (!std::isfinite(value.asDouble())) throw Error(422, "spreadsheet_number");
        const auto number = json(value);
        return ods ? "<table:table-cell office:value-type=\"float\" office:value=\"" + number +
                     "\"><text:p>" + number + "</text:p></table:table-cell>" : "<c><v>" + number + "</v></c>";
    }
    const auto text = cellText(value.asString(), ods);
    return ods ? "<table:table-cell office:value-type=\"string\"><text:p>" + text + "</text:p></table:table-cell>" :
                 "<c t=\"inlineStr\"><is><t xml:space=\"preserve\">" + text + "</t></is></c>";
}
}

Response spreadsheet(const std::string& format, const std::string& name, const Value& columns, const Value& rows) {
    const bool ods = format == "ods";
    WorkbookZip zip;
    std::string sheet;
    if (ods) {
        zip.add("mimetype", odsType, MZ_NO_COMPRESSION);
        zip.add("META-INF/manifest.xml", R"(<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.spreadsheet" manifest:version="1.2"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/></manifest:manifest>)");
        sheet = R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.2"><office:body><office:spreadsheet><table:table table:name="Ledger">)";
        sheet += "<table:table-column table:number-columns-repeated=\"" + std::to_string(columns.size()) + "\"/>";
    } else {
        zip.add("[Content_Types].xml", R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>)");
        zip.add("_rels/.rels", R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>)");
        zip.add("xl/workbook.xml", R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Ledger" sheetId="1" r:id="rId1"/></sheets></workbook>)");
        zip.add("xl/_rels/workbook.xml.rels", R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)");
        sheet = R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>)";
    }
    const auto appendRow = [&](const Value* row) {
        sheet += ods ? "<table:table-row>" : "<row>";
        for (const auto& field : columns) {
            sheet += cell(row ? (*row)[field.asString()] : field, ods);
            if (sheet.size() > exportLimit) throw Error(422, "narrow_filters");
        }
        sheet += ods ? "</table:table-row>" : "</row>";
    };
    appendRow(nullptr);
    for (const auto& row : rows) appendRow(&row);
    sheet += ods ? "</table:table></office:spreadsheet></office:body></office:document-content>" :
                   "</sheetData></worksheet>";
    zip.add(ods ? "content.xml" : "xl/worksheets/sheet1.xml", sheet);
    Response result(200, ods ? odsType : xlsxType, zip.finish());
    result.filename = "pvledger-" + name + "." + format;
    return result;
}
}
