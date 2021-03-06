include Utils.Json.Decode;
module A = Utils.Array;
module D = Utils.Dict;
type dict('a) = D.t('a);

type error =
  | RowDecodeError(int, Js.Json.t, string)
  | EmptyRows;
exception Error(error);

module Row = {
  type t('a) = {
    index: int,
    contents: 'a,
  };
  let make = (index: int, contents: 'a) => {index, contents};
  let map = (row, f) => {...row, contents: f(row.contents)};
  let mapGet = (row, f) => f(row.contents);

  let decodeJson: (t(Js.Json.t), decoder('a)) => 'a =
    (row, decode) =>
      try (mapGet(row, decode)) {
      | DecodeError(err) => raise(Error(RowDecodeError(row.index, row.contents, err)))
      };
};

// Something which can translate rows of Json objects.
type rowsDecoder('t) = array(Row.t(Js.Json.t)) => 't;

// A do-nothing decoder, used for queries which don't return information.
let unit: rowsDecoder(unit) = _ => ();

let encodeError =
  Utils.Json.Encode.(
    fun
    | RowDecodeError(num, rowJson, message) =>
      object_([
        ("number", num |> int),
        ("rowJson", rowJson |> json),
        ("message", message |> string),
      ])
    | EmptyRows => "EmptyRows" |> string
  );

let errorToString: error => string =
  fun
  | RowDecodeError(n, _, str) =>
    "Error occurred when parsing row " ++ string_of_int(n) ++ ": " ++ str
  | EmptyRows => "No rows to parse";

let toRows: array('a) => array(Row.t('a)) = rs => A.mapWithIndex(rs, Row.make);

let getFirst: array(Row.t('a)) => Row.t('a) =
  fun
  | [||] => raise(Error(EmptyRows))
  | rows => rows[0];

// Decode the first row with the given JSON decoder.
let decodeOne: decoder('t) => rowsDecoder('t) =
  (decode, rows) => Row.decodeJson(rows |> getFirst, decode);

// Map a JSON decoder over the rows, collecting the result for each row.
let decodeEach: (decoder('a), array(Row.t(Js.Json.t))) => array('a) =
  (d, arr) => A.map(arr, row => Row.decodeJson(row, d));

let decodeReduce: (decoder('a), 'b, ('a, 'b) => 'b) => rowsDecoder('b) =
  (dec, start, f, rows) => A.reduce(decodeEach(dec, rows), start, f);

let optColumn: (string, decoder('t)) => rowsDecoder(option('t)) =
  (col, dec) =>
    fun
    | [||] => None
    | rows => Some(Row.decodeJson(rows[0], field(col, dec)));

// Get one column, with the given name and with the given decoder.
// Alias for `Json.Decode.field`
let column1: (string, decoder('t)) => decoder('t) = field;

let columns2: (string, decoder('a), string, decoder('b)) => decoder(('a, 'b)) =
  (columnA, decodeA, columnB, decodeB, j) => (
    j |> field(columnA, decodeA),
    j |> field(columnB, decodeB),
  );

let columns3:
  (string, decoder('a), string, decoder('b), string, decoder('c)) => decoder(('a, 'b, 'c)) =
  (columnA, decodeA, columnB, decodeB, columnC, decodeC, j) => (
    j |> field(columnA, decodeA),
    j |> field(columnB, decodeB),
    j |> field(columnC, decodeC),
  );

// Given a row where one of the fields is an ID, a decoder for
// the ID and another decoder to get the rest of the object, decodes
// the row into the object.
let withId =
    (~idField: string, ~idDecode: decoder('id), decode: decoder('t)): decoder(('id, 't)) =>
  tup2(field(idField, idDecode), decode);

// Decode a row into a 3-tuple.
let tuple3Row:
  (string, decoder('a), string, decoder('b), string, decoder('c)) => decoder(('a, 'b, 'c)) =
  (columnA, decodeA, columnB, decodeB, columnC, decodeC, j) => (
    j |> field(columnA, decodeA),
    j |> field(columnB, decodeB),
    j |> field(columnC, decodeC),
  );

// Given a way to get a (key, value) pair from a row, produce a
// dictionary with those keys/values.
let dict =
    (
      ~keyField: string,
      ~keyDecode: decoder(string)=string,
      ~valueField: string,
      ~valueDecode: decoder('a),
    )
    : rowsDecoder(D.t('a)) =>
  jsonRows => {
    jsonRows
    |> decodeEach(tup2(field(keyField, keyDecode), field(valueField, valueDecode)))
    |> D.fromArray;
  };

let dict2d =
    (
      ~outerKeyField: string,
      ~outerKeyDecode: decoder(string)=string,
      ~innerKeyField: string,
      ~innerKeyDecode: decoder(string)=string,
      ~valueField: string,
      ~valueDecode: decoder('a),
    )
    : rowsDecoder(D.t(D.t('a))) =>
  jsonRows =>
    jsonRows
    |> decodeEach(
         tup3(
           field(innerKeyField, innerKeyDecode),
           field(outerKeyField, outerKeyDecode),
           field(valueField, valueDecode),
         ),
       )
    |> (
      decoded => {
        let result = D.empty();
        A.forEach(decoded, ((inner, outer, value)) =>
          switch (D.get(result, outer)) {
          | None => D.set(result, outer, D.fromArray([|(inner, value)|]))
          | Some(values) => D.set(values, inner, value)
          }
        );
        result;
      }
    );

let dict3d =
    (
      ~keyField1: string,
      ~keyDecode1: decoder(string)=string,
      ~keyField2: string,
      ~keyDecode2: decoder(string)=string,
      ~keyField3: string,
      ~keyDecode3: decoder(string)=string,
      ~valueField: string,
      ~valueDecode: decoder('a),
    )
    : rowsDecoder(dict(dict(dict('a)))) =>
  jsonRows =>
    jsonRows
    |> decodeEach(
         tup4(
           field(keyField1, keyDecode1),
           field(keyField2, keyDecode2),
           field(keyField3, keyDecode3),
           field(valueField, valueDecode),
         ),
       )
    |> (
      decoded => {
        let result: dict(dict(dict('a))) = D.empty();
        A.forEach(decoded, ((x, y, z, value)) =>
          switch (D.get(result, x)) {
          | None => D.set(result, x, D.singleton(y, D.singleton(z, value)))
          | Some(xValues) =>
            switch (D.get(xValues, y)) {
            | None => D.set(xValues, y, D.singleton(z, value))
            | Some(yValues) => D.set(yValues, z, value)
            }
          }
        );
        result;
      }
    );

// TODO there might be a way to DRY this up
let dict4d =
    (
      ~keyField1: string,
      ~keyDecode1: decoder(string)=string,
      ~keyField2: string,
      ~keyDecode2: decoder(string)=string,
      ~keyField3: string,
      ~keyDecode3: decoder(string)=string,
      ~keyField4: string,
      ~keyDecode4: decoder(string)=string,
      ~valueField: string,
      ~valueDecode: decoder('a),
    )
    : rowsDecoder(dict(dict(dict(dict('a))))) =>
  jsonRows =>
    jsonRows
    |> decodeEach(
         tup5(
           field(keyField1, keyDecode1),
           field(keyField2, keyDecode2),
           field(keyField3, keyDecode3),
           field(keyField4, keyDecode4),
           field(valueField, valueDecode),
         ),
       )
    |> (
      decoded => {
        let result: dict(dict(dict(dict('a)))) = D.empty();
        A.forEach(decoded, ((k1, k2, k3, k4, value)) =>
          switch (D.get(result, k1)) {
          | None => D.set(result, k1, D.singleton(k2, D.singleton(k3, D.singleton(k4, value))))
          | Some(values1) =>
            switch (D.get(values1, k2)) {
            | None => D.set(values1, k2, D.singleton(k3, D.singleton(k4, value)))
            | Some(values2) =>
              switch (D.get(values2, k3)) {
              | None => D.set(values2, k3, D.singleton(k4, value))
              | Some(values3) => D.set(values3, k4, value)
              }
            }
          }
        );
        result;
      }
    );
