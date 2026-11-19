/*
EXEMPLO DE PRÓXIMA ETAPA — integração com Supabase

1. Crie um projeto no Supabase e uma tabela chamada "medicoes".
2. Exemplo de colunas:
   id            uuid / chave primária
   torre         text ("Alfa" ou "Beta")
   umidade       numeric
   luminosidade  numeric
   condutividade numeric
   registrado_em timestamptz
3. Adicione @supabase/supabase-js ao projeto (por CDN ou gerenciador de pacotes).
4. Use a URL do projeto e a chave anon/public; nunca exponha a chave service_role.
5. Configure políticas RLS adequadas antes de publicar.

Exemplo (não funciona até inserir suas credenciais e configurar a tabela):

const SUPABASE_URL = "SUA_URL_DO_PROJETO";
const SUPABASE_ANON_KEY = "SUA_CHAVE_ANON_PUBLIC";

// Depois de carregar o SDK:
// const supabase = window.supabase.createClient(SUPABASE_URL, SUPABASE_ANON_KEY);
// const { data, error } = await supabase
//   .from("medicoes")
//   .select("*")
//   .order("registrado_em", { ascending: false })
//   .limit(50);

As medições a cada 30 minutos precisam ser feitas pelo dispositivo/controlador
ou por um processo agendado apropriado. O front-end apenas consulta e apresenta
os dados; não deve ser responsável sozinho por garantir a coleta.
*/
