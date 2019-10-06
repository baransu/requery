module O = Belt.Option;
let (resolve, then_, reject) = Js.Promise.(resolve, then_, reject);

type config = {
  host: string,
  database: string,
  port: int,
};

type error =
  | RowDecodeError(RowDecode.error);

let encodeError: Utils.Json.encoder(error) =
  Utils.Json.Encode.(
    fun
    | RowDecodeError(e) => e |> object1("RowDecodeError", RowDecode.encodeError)
  );

exception Error(error);

module QueryResult = {
  type t('a) =
    | Error(error)
    | Success('a);

  module Enc = Utils.Json.Encode;

  let encode: Utils.Json.encoder('a) => Utils.Json.encoder(t('a)) =
    Enc.(
      encodeSuccess =>
        fun
        | Error(e) => e |> object1("Error", encodeError)
        | Success(x) => x |> object1("Success", encodeSuccess)
    );

  let unwrap: t('a) => 'a =
    fun
    | Success(x) => x
    | Error(err) => raise(Error(err));

  let unwrapPromise: ('a => Js.Promise.t('b), t('a)) => Js.Promise.t('b) =
    next =>
      fun
      | Success(x) => next(x)
      | Error(err) => reject(Error(err));
};

// This could be a functor, parameterized on an error type. Also, need
// to think about error handling.
module type DBType = {
  type result;
  type pool;
  type client;

  let resultToRows: result => array(RowDecode.Row.t(Js.Json.t));
  let makePool: config => pool;
  let makeClient: pool => Js.Promise.t(client);
  let releaseClient: client => Js.Promise.t(unit);
  let releasePool: pool => Js.Promise.t(unit);
  let query: (client, Sql.query) => Js.Promise.t(result);
};

module Query = (DB: DBType) => {
  include DB;

  // Execute an arbitrary query, decoding the result.
  let query =
      (
        ~logQuery=?,
        ~logResult=?,
        client: DB.client,
        decode: RowDecode.decodeRows('t),
        query: Sql.query,
      )
      : Js.Promise.t(QueryResult.t('t)) => {
    let _ = O.map(logQuery, f => f(query));
    DB.query(client, query)
    |> then_((result: DB.result) => {
         let _ = O.map(logResult, f => f(result));
         resolve(
           try (QueryResult.Success(decode(DB.resultToRows(result)))) {
           | RowDecode.Error(e) => QueryResult.Error(RowDecodeError(e))
           },
         );
       });
  };

  // Execute a SELECT, decoding the result.
  let select =
      (
        ~logQuery=?,
        ~logResult=?,
        client: DB.client,
        decode: RowDecode.decodeRows('t),
        select: QueryBuilder.select,
      )
      : Js.Promise.t(QueryResult.t('t)) =>
    query(~logQuery?, ~logResult?, client, decode, Sql.Select(select));

  // Same as `select` but using the Retrieval.t abstraction.
  let retrieve =
      (
        ~logQuery=?,
        ~logResult=?,
        client: DB.client,
        {toSelect, decode}: Retrieval.t('args, 'result),
        args: 'args,
      )
      : Js.Promise.t(QueryResult.t('result)) => {
    select(~logQuery?, ~logResult?, client, decode, toSelect(args));
  };

  // Run an INSERT query, decoding whatever's returned. Note: if nothing is
  // returned from the insertion query, the decoder can be `RowDecode.unit`.
  let insert =
      (
        ~logQuery=?,
        ~logResult=?,
        client: DB.client,
        decode: RowDecode.decodeRows('t),
        insert: QueryBuilder.insert,
      )
      : Js.Promise.t(QueryResult.t('t)) =>
    query(~logQuery?, ~logResult?, client, decode, Sql.Insert(insert));
};
