"use strict";

const fail = (code) => { throw new Error(code); };
const check = (ok, code = "invalid_file") => { if (!ok) fail(code); };
const rowLimit = 500;

// Parse quoted CSV records, including embedded line endings and doubled quotes.
function csvRecords(text, delimiter) {
  const rows = [];
  let row = [], value = "", quoted = false, closed = false;
  const cell = () => {
    check(row.length < 128, "import_limit");
    row.push(value);
    value = "";
    closed = false;
  };
  const end = () => {
    cell();
    if (row.length > 1 || row[0] !== "") rows.push(row);
    check(rows.length <= rowLimit + 1, "import_limit");
    row = [];
  };
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c !== '"') value += c;
      else if (text[i + 1] === '"') { value += '"'; i++; }
      else { quoted = false; closed = true; }
    } else if (c === delimiter) cell();
    else if (c === '\r' || c === '\n') {
      if (c === '\r' && text[i + 1] === '\n') i++;
      end();
    } else if (c === '"' && !value && !closed) quoted = true;
    else {
      check(!closed && c !== '"');
      value += c;
    }
  }
  check(!quoted);
  if (row.length || value || closed) end();
  return rows;
}

function qifRecords(text) {
  const rows = [];
  let mode = "", fields = {}, problem = "", account = "", declaredAccount = "";
  for (const line of text.split(/\r\n|\n|\r/)) {
    if (!line.trim()) continue;
    if (line.startsWith("!")) {
      check(!Object.keys(fields).length && !problem);
      if (/^!Account$/i.test(line)) {
        check(!rows.length, "single_statement");
        mode = "account";
      }
      else if (/^!Type:(Bank|Cash|CCard)$/i.test(line)) mode = "transactions";
      else fail("unsupported_record");
      continue;
    }
    check(mode);
    if (line === "^") {
      if (mode === "account") {
        check(fields.N && (!declaredAccount || declaredAccount === fields.N), "single_statement");
        declaredAccount = fields.N;
        account = fields.N;
      } else rows.push({date: fields.D, amount: fields.T ?? fields.U, name: fields.P || fields.M,
                        description: fields.M, category: fields.L, source_account: account,
                        error: problem || (fields.L?.startsWith("[") ? "unsupported_record" : "")});
      fields = {}; problem = "";
      check(rows.length <= rowLimit, "import_limit");
      continue;
    }
    const key = line[0];
    if (mode === "transactions" && (!"DTPMULCN".includes(key) || key in fields)) problem = "unsupported_record";
    fields[key] = line.slice(1).trim();
    if (fields.T && fields.U && fields.T !== fields.U) problem = "invalid_expense";
  }
  check(!Object.keys(fields).length && !problem);
  return rows;
}

// OFX 1.x permits omitted closing tags only on scalar elements. Convert that subset
// to XML, leaving aggregate nesting and explicit closing tags for DOMParser to verify.
function ofxDocument(text) {
  check(!/<!DOCTYPE|<!ENTITY/i.test(text));
  const start = text.search(/<OFX\s*>/i);
  check(start >= 0);
  const header = text.slice(0, start).trim();
  const sgml = /^OFXHEADER:\s*100\s*$/im.test(header);
  if (!sgml) check(!header || /^(?:<\?(?:xml|OFX)\s+[^?]*\?>\s*)+$/i.test(header));
  let xml = text.slice(start);
  if (sgml) {
    check(header.split(/\r?\n/).every(line => /^[A-Z0-9]+:[^<>]*$/i.test(line.trim())));
    check(/^DATA:\s*OFXSGML\s*$/im.test(header));
    xml = xml.replace(/<([A-Za-z][\w.]*)>([^<>]+)(?=<)/g, (whole, tag, raw, index, source) => {
      if (!raw.trim() || source.slice(index + whole.length).startsWith("</" + tag + ">")) return whole;
      return "<" + tag + ">" + raw.trimEnd() + "</" + tag + ">";
    });
  }
  const document = new DOMParser().parseFromString(xml, "application/xml");
  check(!document.querySelector("parsererror") && document.documentElement.tagName === "OFX");
  return document;
}

function ofxRecords(text) {
  const doc = ofxDocument(text);
  const statements = doc.querySelectorAll("STMTRS, CCSTMTRS");
  check(statements.length === 1, "single_statement");
  const statement = statements[0];
  check(!doc.querySelector("INVSTMTRS, INVBANKTRAN, STMTTRNP, BANKTRANLISTP"), "unsupported_record");
  const field = (parent, tag) => {
    const elements = parent.querySelectorAll(":scope > " + tag);
    check(elements.length <= 1);
    return elements[0]?.textContent.trim();
  };
  const currency = field(statement, "CURDEF");
  check(currency, "currency_missing");
  const lists = statement.querySelectorAll(":scope > BANKTRANLIST");
  check(lists.length === 1);
  const transactions = lists[0].querySelectorAll(":scope > STMTTRN");
  check(transactions.length === doc.querySelectorAll("STMTTRN").length);
  check(transactions.length <= rowLimit, "import_limit");
  for (const child of lists[0].children)
    check(["DTSTART", "DTEND", "STMTTRN"].includes(child.tagName), "unsupported_record");
  const account = statement.querySelector("BANKACCTFROM > ACCTID, CCACCTFROM > ACCTID")?.textContent.trim();
  const expenseTypes = ["DEBIT", "CHECK", "PAYMENT", "CASH", "DIRECTDEBIT", "FEE", "SRVCHG", "ATM", "POS", "REPEATPMT", "OTHER"];
  return Array.from(transactions, (transaction) => ({
    name: field(transaction, "NAME") || field(transaction, "MEMO"), description: field(transaction, "MEMO"),
    date: field(transaction, "DTPOSTED"), amount: field(transaction, "TRNAMT"), source_id: field(transaction, "FITID"),
    source_account: account, currency, error: !expenseTypes.includes(field(transaction, "TRNTYPE")) ||
      transaction.querySelector("CURRENCY, ORIGCURRENCY, CORRECTFITID, CORRECTACTION, SPLIT, BANKACCTTO, CCACCTTO")
      ? "unsupported_record" : ""
  }));
}

export function parseFile(text, format, options) {
  check(text && !text.includes("\0") && !text.includes("\uFFFD"));
  text = text.replace(/^\uFEFF/, "");
  let records, columns = [];
  if (format === "csv") {
    records = csvRecords(text, options.delimiter);
    check(records.length, "empty_file");
    columns = options.header ? records.shift() : records[0].map((_, i) => String(i + 1));
    check(columns.length && records.length <= rowLimit, "import_limit");
  } else if (format === "qif") records = qifRecords(text);
  else if (format === "ofx" || format === "qfx") records = ofxRecords(text);
  else fail("unsupported_record");
  check(records.length, "empty_file");
  return {records, columns};
}

function timestamp(raw, format) {
  const text = String(raw ?? "").trim();
  if (format === "unix") {
    check(/^-?\d+$/.test(text), "invalid_date");
    const n = Number(text);
    check(Number.isSafeInteger(n) && n >= -62135596800 && n <= 253402300799, "invalid_date");
    return n;
  }
  let y, m, d, hour = 0, minute = 0, second = 0, offset = 0;
  if (format === "ofx") {
    const match = /^(\d{4})(\d{2})(\d{2})(?:(\d{2})(\d{2})(\d{2})(?:\.(\d+))?)?(?:\[([+-]?\d+(?:\.\d+)?)(?::[^\]]+)?\])?$/.exec(text);
    check(match && (!match[7] || /^0+$/.test(match[7])), "invalid_date");
    [, y, m, d] = match;
    hour = Number(match[4] || 0); minute = Number(match[5] || 0); second = Number(match[6] || 0);
    offset = Number(match[8] || 0) * 3600;
    check(Number.isInteger(offset) && Math.abs(offset) <= 14 * 3600, "invalid_date");
  } else if (format === "iso") {
    const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(text);
    check(match, "invalid_date");
    [, y, m, d] = match;
  } else {
    const match = /^(\d{1,2})\s*[/.-]\s*(\d{1,2})\s*(?:[/.-]|')\s*(\d{4}|\d{2})$/.exec(text);
    check(match && ["mdy", "dmy"].includes(format), "invalid_date");
    m = format === "mdy" ? match[1] : match[2];
    d = format === "mdy" ? match[2] : match[1];
    y = Number(match[3]);
    if (match[3].length === 2) y += y >= 70 ? 1900 : 2000;
  }
  const date = new Date(0);
  date.setUTCFullYear(Number(y), Number(m) - 1, Number(d));
  date.setUTCHours(hour, minute, second);
  check(+y > 0 && date.getUTCFullYear() === +y && date.getUTCMonth() === +m - 1 && date.getUTCDate() === +d &&
        hour < 24 && minute < 60 && second < 60, "invalid_date");
  return date.getTime() / 1000 - offset;
}

export function mapRows(file, options) {
  return file.records.map((record) => {
    const row = {};
    try {
      if (Array.isArray(record)) {
        check(record.length === file.columns.length, "invalid_columns");
        for (const [key, column] of Object.entries(options.mapping)) if (column !== "") row[key] = record[Number(column)];
      } else Object.assign(row, record);
      if (row.error) return row;
      row.date = timestamp(row.date, options.date);
      let amount = String(row.amount ?? "").trim();
      if (options.decimal === ",") {
        check(!amount.includes("."), "invalid_amount");
        amount = amount.replace(",", ".");
      }
      check(/^[+-]?(?:\d+(?:\.\d+)?|\.\d+)$/.test(amount), "invalid_amount");
      row.amount = Number(amount) * (options.sign === "negative" ? -1 : 1);
      check(Number.isFinite(row.amount) && row.amount > 0, "wrong_sign");
      if (row.currency) row.currency = row.currency.trim().toUpperCase();
      else delete row.currency;
    } catch (error) { row.error = error.message; }
    return row;
  });
}
