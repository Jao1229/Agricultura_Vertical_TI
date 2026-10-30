const SUPABASE_URL = 'SUA_URL';
const SUPABASE_KEY = 'SUA_CHAVE_PUBLICA';

const supabaseClient = supabase.createClient(
  SUPABASE_URL,
  SUPABASE_KEY
);

async function testarConexao() {
  const { data, error } = await supabaseClient
    .from('medicoes')
    .select('*')
    .limit(1);

  if (error) {
    console.error('Erro na conexão:', error);
    return;
  }

  console.log('Conexão realizada com sucesso!');
  console.log('Dados recebidos:', data);
}

testarConexao();