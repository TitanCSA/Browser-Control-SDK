# Automatiser votre première page sous Windows

Ce parcours utilise Browser Control SDK avec Python. Il ne nécessite ni compilation ni téléchargement des sources.

## 1. Ouvrir le navigateur

1. Téléchargez le **ZIP Windows x64 complet** depuis les [Releases officielles](https://github.com/TitanCSA/Browser-Control-SDK/releases/latest).
2. Extrayez tout le ZIP dans un dossier local. Ne lancez pas l'application depuis l'archive compressée.
3. Ouvrez **BrowserControl.exe** et gardez sa fenêtre ouverte.

Si l'application demande Microsoft Edge WebView2, suivez son lien de téléchargement officiel Microsoft, puis relancez-la. Aucun pilote d'entrée ni droit administrateur n'est nécessaire pour ce navigateur.

## 2. Installer le client Python

Avec Python 3.10 ou plus récent, ouvrez PowerShell et lancez :

```powershell
python -m pip install browser-control-sdk
```

Le paquet installe le **client SDK**, pas l'application BrowserControl.exe. Les deux étapes sont nécessaires.

## 3. Lancer un script

Créez un fichier `premiere_page.py` contenant :

```python
from browser_control import Browser

navigateur = Browser.connect()
onglet = navigateur.new_tab()
onglet.goto("https://example.com")
print(onglet.snapshot()["text"])
onglet.screenshot(path="premiere-page.png")
```

Dans le dossier où vous avez enregistré ce fichier :

```powershell
python premiere_page.py
```

Un onglet s'ouvre dans Browser Control, le texte de la page apparaît dans le terminal et `premiere-page.png` est enregistré dans le dossier courant. Le navigateur reste ouvert : vous pouvez continuer à l'utiliser manuellement.

## En cas de problème

| Ce que vous voyez | Ce qu'il faut vérifier |
| --- | --- |
| Python n'est pas reconnu | Installez Python 3.10 ou plus récent, puis rouvrez PowerShell. |
| `No module named browser_control` | Installez le paquet avec le même `python` que celui qui exécute le script. |
| Aucun navigateur actif n'est trouvé | Ouvrez BrowserControl.exe, attendez sa fenêtre et lancez le script avec le même compte Windows. |
| Aucun fichier PNG dans le dossier attendu | La capture est enregistrée dans le dossier courant du terminal, qui peut différer du dossier du script. |
| Une connexion à un site est demandée | Connectez-vous normalement dans le profil Browser Control choisi. Les profils Chrome et Edge existants ne sont pas importés. |

## Étape suivante : formulaires et clics

Le [guide principal](../README.md#learn-form-interaction-on-a-local-page) montre une page de démonstration locale avec un champ et un bouton. Ce second exercice nécessite les sources et leur petit serveur de démonstration. Le serveur local n'est pas nécessaire pour le premier script ci-dessus.

Pour d'autres langages et les méthodes disponibles : [référence SDK](../SDK/README.md), [API](../Protocol/API.md), [MCP](../MCP/README.md).

Browser Control fournit son propre navigateur WebView2. Ce n'est pas le produit Chrome ou Edge complet : les extensions, les profils existants et la compatibilité avec tous les sites ne sont pas garantis. Lisez les [limites et règles de stockage](SECURITY.md).
